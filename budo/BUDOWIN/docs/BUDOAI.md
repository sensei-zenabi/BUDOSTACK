# BUDOAI v1 architecture

BUDOAI.BWA is a native C11/POSIX module requiring BWA ABI 1.19. The build
includes it in APPS; ordinary desktop module discovery supplies its launcher.
Each instance owns its window, engine, history, index and directory handles.
No network, model, database, subprocess or third-party runtime is involved.

## Paths and knowledge contract

The default root is `./knowledge` relative to `get_working_directory()`.
In a normal BUDOSTACK launch this is `users/default/knowledge`, containing
seven replaceable sample TXT files. Standalone launches use the initial user
workspace. `BUDOAI_KNOWLEDGE_ROOT` overrides the initial root. The editable
root field accepts absolute or workspace-relative paths. Enter in that field,
Refresh Knowledge or F5 cancels current work, clears category context and
rebuilds the in-memory index. No source file is written and no index is saved.

Subdirectories supply category paths, with no configured category list.
Discovery accepts case-insensitive `.txt` suffixes. Directory and file
symlinks, FIFOs, devices and other nonregular files are excluded. Text is
intended for English ASCII, LF or CRLF. UTF-8 bytes are retained in excerpts
but non-ASCII words are not linguistically indexed. NUL and unexpected control
bytes stop that file, report an error, and preserve previously indexed passages.

## State machine and bounds

`READY -> INDEX -> READY` walks at most sixteen directory levels. One indexing
step handles one directory entry or at most 640 source bytes. Blank lines
separate passages; long paragraphs split after 480 bytes at whitespace, or
at 640 bytes. A long word can therefore split across passages. The index
keeps file paths, byte and line ranges, checksums, normalized term counts
adjacent term hashes and narrow copula relations; it does not retain the complete text corpus.

`READY -> DF -> SCORE -> READY` computes query document frequencies over
passages, then scores 32 passages per engine step. A desktop tick performs
at most eight steps. Finalization rereads at most 24 candidate passages.
Regular local file operations are synchronous, so slow storage can still
stall an individual operation. There are no worker threads.

Bounds are 256 documents, 4096 passages, 2 MiB read per file (plus at most one
640-byte chunk), 32 MiB total scanned text, 24 unique query terms, and six
answer excerpts. Passage arrays grow geometrically; maximum index allocation
is less than 8 MiB plus 1 MiB of document paths and about 0.5 MiB of spelling vocabulary. The status bar exposes
errors and resource-limit truncation; indexing is partial when a bound is hit.
Discovery order at a resource limit depends on filesystem enumeration order.

History is a 64 KiB rolling text buffer; oldest text is discarded on overflow.
Each entry contains the submitted question, extractive answer and references.
Clear or Ctrl+L clears history and category context; closing frees resources.
Tab/Shift+Tab move between fields, controls and answers. Enter submits; the
answer viewport supports wheel, Page Up/Down, focused arrows and a scrollbar.

## Retrieval and grounding

ASCII case folding, common stop words, plural and suffix rules supply basic
lexical normalization. Small, topic-independent equivalence classes cover
common wording such as stuff/gear/equipment and remember/memory. These are
lexical aliases, not semantic inference. Query-only filler removal handles
go/start/begin in requests for a procedure.

The index learns up to 4096 raw words and their normalized hashes in an
8192-slot hash table. Unknown query words of at least five characters may
be corrected by one insertion, deletion, substitution or adjacent
transposition. Corrections must have a unique normalized target across the
indexed vocabulary. Ambiguous corrections are rejected, and applied changes
are shown with the answer. This does not use a fixed subject dictionary.

FNV-1a 64-bit token hashes keep storage bounded (hash collisions are
theoretically possible). BM25 uses k1=1.2, b=0.75, passage-frequency IDF and
passage length normalization. Normalized adjacent query pairs boost scores.
Headings alone cannot answer a question. Short queries require all distinct
content terms in a passage; longer ones require two-thirds coverage.

Question intent constrains selection: procedures need instruction markers,
why questions need explicit causal wording, equipment questions need equipment
cues, and simpler explanations need a source labelled `Simple explanation:`
or `In simple words:`. These optional labels are plain TXT, not question/answer
records. Procedures recognize Step/Procedure/Instructions labels, numbered
lists, or first/then/finally language. Ordinary unlabelled factual TXT remains
searchable without annotations. A bounded set of copula subject/predicate
head pairs allows narrow identity questions, while rejecting reversed
relations. More complex yes/no questions may still be declined.

Recent category context boosts short follow-ups by 25 percent but cannot
create a lexical match. Successful query terms also identify the subject of
requests such as "explain that more simply". These requests retrieve an
existing simpler passage; the engine does not rewrite or generate one.
Failed queries and Clear remove the subject used for simplification. Unresolved
pronouns and broader paraphrases can still fail.

Candidate ties use pathname and byte offset. Near-identical term sets are
deduplicated. Passages below 22 percent of the best score are omitted.
Procedural excerpts are grouped by document relevance, then ordered by source
offset, preserving numbered steps. Other queries retain relevance order. Each answer labels excerpts as source
material and includes relative filename, one-based inclusive line range and
zero-based inclusive byte range internally. The normal UI shows filename and
line range, keeping byte offsets out of the reading flow. File identity, size and nanosecond modification timestamps plus passage
checksums reject removed or changed sources before display; old history is a snapshot and may outlive source edits.

The initial answer shows one excerpt, limited to 400 characters at a word
boundary. More (Ctrl+D) reveals additional excerpts or the complete first
passage. More rechecks file identity/timestamps and passage bytes before
displaying additional text. This keeps short answers readable while retaining multiple-source
retrieval. No unsupported connective facts are generated. A no-match response describes
insufficient matching information, rather than claiming the corpus contains
no possible answer. Relevant text may still fail to answer a question, and
English normalization and conservative coverage can miss valid paraphrases.

## Validation

`tests/budowin_budoai.c` tests sample and arbitrary TXT retrieval, follow-up
context, source byte/line verification, refresh, deduplication, missing and
changed files, malformed content, ignored symlink/FIFO entries, bounded large
input, empty/missing roots and UI callbacks. `tests/run_budowin.sh` runs it
alongside existing regressions. Native host tests load the actual BWA, submit
through button dispatch and can capture a framebuffer for visual inspection.

## Child-style acceptance specification

`tests/budowin_budoai_child.c` exercises the same 32 simulated child-style
questions used to evaluate the first build. A positive case requires the
primary excerpt to contain the answer, not merely a topic keyword somewhere
in the result set. Procedural cases require all three steps in that primary
excerpt. Responses must stay below 700 characters including the question,
spelling notice and source, show only the first source initially, and retain
verifiable source bytes and line ranges. The ten unsupported/social questions must produce either a clear no-answer
response or application guidance, without source excerpts. Greetings and
self-identity use ordinary application messages about BUDOAI itself; they do
not supply subject facts or replace retrieval. This is a local-book assistant,
not a general chat or joke-writing system.

Additional tests load an unseen space/sensor/lens TXT document without code
changes and check new-vocabulary spelling, simple explanations, plain
first/then instructions, supported and reversed identity relations,
ambiguous spellings, unsupported causes, More, and clearing a failed topic.
The benchmark is a regression target, not validation by actual children or
a guarantee of comprehension for arbitrary wording. Source content must
supply both accurate facts and readable explanations; no runtime model can
simplify arbitrary advanced source prose in this version.
