#!/usr/bin/env python3
"""Compile/link terminal shaders on headless Mesa OpenGL 2.1 and 4.5.

Requires Python's standard library, EGL/OpenGL runtime libraries and Mesa's
software driver. Run with: python3 tests/terminal_shader_compat.py
No SDL development headers or display server are needed.
"""
import ctypes as C
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def function(library, name, result, *arguments):
    value = getattr(library, name)
    value.restype = result
    value.argtypes = arguments
    return value


def check_context(mode):
    egl = C.CDLL("libEGL.so.1")
    gl = C.CDLL("libGL.so.1")
    P, I, U, F = C.c_void_p, C.c_int, C.c_uint, C.c_float
    display = function(egl, "eglGetDisplay", P, P)(None)
    major, minor = I(), I()
    assert function(egl, "eglInitialize", U, P, C.POINTER(I), C.POINTER(I))(
        display, C.byref(major), C.byref(minor)), "EGL initialization failed"
    surface, context = None, None
    try:
        assert function(egl, "eglBindAPI", U, U)(0x30A2)  # EGL_OPENGL_API
        # Pbuffer surface, desktop OpenGL, eight red bits, EGL_NONE.
        attributes = (I * 7)(0x3033, 1, 0x3040, 8, 0x3024, 8, 0x3038)
        config, count = P(), I()
        assert function(egl, "eglChooseConfig", U, P, C.POINTER(I), C.POINTER(P), I, C.POINTER(I))(
            display, attributes, C.byref(config), 1, C.byref(count)) and count.value
        context = function(egl, "eglCreateContext", P, P, P, P, C.POINTER(I))(
            display, config, None, (I * 1)(0x3038))
        surface = function(egl, "eglCreatePbufferSurface", P, P, P, C.POINTER(I))(
            display, config, (I * 5)(0x3057, 16, 0x3056, 16, 0x3038))
        assert context and surface
        make_current = function(egl, "eglMakeCurrent", U, P, P, P, P)
        assert make_current(display, surface, surface, context)
        get_string = function(gl, "glGetString", C.c_char_p, U)
        version = get_string(0x1F02).decode()
        language = get_string(0x8B8C).decode()
        assert version.startswith("2.1" if mode == "legacy" else "4.5"), version
        assert language.startswith("1.20" if mode == "legacy" else "4.50"), language
        shader_source = function(gl, "glShaderSource", None, U, I, C.POINTER(C.c_char_p), P)
        get_shader = function(gl, "glGetShaderiv", None, U, U, C.POINTER(I))
        get_log = function(gl, "glGetShaderInfoLog", None, U, I, P, C.c_char_p)
        delete_shader = function(gl, "glDeleteShader", None, U)
        delete_program = function(gl, "glDeleteProgram", None, U)
        create_shader = function(gl, "glCreateShader", U, U)
        compile_shader = function(gl, "glCompileShader", None, U)

        def compile_stage(text, stage, define, reject=False):
            match = re.match(r"#version[^\n]*\n", text)
            prefix = match.group() if match else "#version 110\n"
            body = text[match.end():] if match else text
            source = (prefix + "#define PARAMETER_UNIFORM 1\n#define " + define + " 1\n" + body).encode()
            shader = create_shader(stage)
            shader_source(shader, 1, C.byref(C.c_char_p(source)), None)
            compile_shader(shader)
            status = I()
            get_shader(shader, 0x8B81, C.byref(status))  # GL_COMPILE_STATUS
            log = C.create_string_buffer(8192)
            get_log(shader, len(log), None, log)
            if reject:
                delete_shader(shader)
                assert not status.value and b"not supported" in log.value, log.value
                return None
            assert status.value, log.value.decode()
            return shader

        def link(text):
            vertex = compile_stage(text, 0x8B31, "VERTEX")
            fragment = compile_stage(text, 0x8B30, "FRAGMENT")
            program = function(gl, "glCreateProgram", U)()
            attach = function(gl, "glAttachShader", None, U, U)
            attach(program, vertex)
            attach(program, fragment)
            function(gl, "glLinkProgram", None, U)(program)
            status = I()
            function(gl, "glGetProgramiv", None, U, U, C.POINTER(I))(program, 0x8B82, C.byref(status))
            delete_shader(vertex)
            delete_shader(fragment)
            log = C.create_string_buffer(8192)
            function(gl, "glGetProgramInfoLog", None, U, I, P, C.c_char_p)(program, len(log), None, log)
            assert status.value, log.value.decode()
            return program

        def render_noise(program, text, frame, toggle):
            function(gl, "glUseProgram", None, U)(program)
            location = function(gl, "glGetUniformLocation", I, U, C.c_char_p)
            uniform_float = function(gl, "glUniform1f", None, I, F)
            for name, default in re.findall(r'#pragma parameter (\w+) "[^"]+" ([\d.-]+)', text):
                uniform_float(location(program, name.encode()), float(default))
            uniform_float(location(program, b"noise_toggle"), toggle)
            function(gl, "glUniform1i", None, I, I)(location(program, b"FrameCount"), frame)
            for name in (b"TextureSize", b"InputSize", b"OutputSize"):
                function(gl, "glUniform2f", None, I, F, F)(location(program, name), 16, 16)
            matrix = (F * 16)(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
            function(gl, "glUniformMatrix4fv", None, I, I, C.c_ubyte, C.POINTER(F))(
                location(program, b"MVPMatrix"), 1, 0, matrix)
            texture = U()
            function(gl, "glGenTextures", None, I, C.POINTER(U))(1, C.byref(texture))
            function(gl, "glBindTexture", None, U, U)(0x0DE1, texture)
            parameter = function(gl, "glTexParameteri", None, U, U, I)
            for name in (0x2800, 0x2801):
                parameter(0x0DE1, name, 0x2600)  # nearest min/mag filtering
            pixels = (C.c_ubyte * 1024)(*(value for y in range(16) for x in range(16)
                                         for value in (x * 17, y * 17, (x + y) * 8, 255)))
            function(gl, "glTexImage2D", None, U, I, I, I, I, I, U, U, P)(
                0x0DE1, 0, 0x1908, 16, 16, 0, 0x1908, 0x1401, pixels)
            positions = (F * 16)(-1, -1, 0, 1, 1, -1, 0, 1, -1, 1, 0, 1, 1, 1, 0, 1)
            coords = (F * 8)(0, 0, 1, 0, 0, 1, 1, 1)
            enabled = []
            for name, size, data in ((b"VertexCoord", 4, positions), (b"TexCoord", 2, coords)):
                index = function(gl, "glGetAttribLocation", I, U, C.c_char_p)(program, name)
                assert index >= 0
                enabled.append(index)
                function(gl, "glEnableVertexAttribArray", None, U)(index)
                function(gl, "glVertexAttribPointer", None, U, I, U, C.c_ubyte, I, P)(
                    index, size, 0x1406, 0, 0, data)
            function(gl, "glViewport", None, I, I, I, I)(0, 0, 16, 16)
            function(gl, "glDrawArrays", None, U, I, I)(0x0005, 0, 4)  # triangle strip
            output = (C.c_ubyte * 1024)()
            function(gl, "glReadPixels", None, I, I, I, I, U, U, P)(0, 0, 16, 16, 0x1908, 0x1401, output)
            assert function(gl, "glGetError", U)() == 0
            for index in enabled:
                function(gl, "glDisableVertexAttribArray", None, U)(index)
            function(gl, "glDeleteTextures", None, I, C.POINTER(U))(1, C.byref(texture))
            return bytes(output)

        noise = (ROOT / "budo/shaders/noise.glsl").read_text()
        assert noise.startswith("#version 120\n")
        previous = noise.replace("#version 120", "#version 130", 1)
        if mode == "legacy":
            for stage, define in ((0x8B31, "VERTEX"), (0x8B30, "FRAGMENT")):
                compile_stage(previous, stage, define, reject=True)
        for name in ("noise", "effects", "crtscreen"):
            text = (ROOT / "budo/shaders" / (name + ".glsl")).read_text()
            program = link(text)
            if name == "noise":
                outputs = [render_noise(program, text, frame, toggle)
                           for frame, toggle in ((0, 0), (47, 1), (92, 1))]
                assert any(outputs[0][i] for i in range(0, 1024, 4)), "No rendered output"
                if mode == "modern":
                    original = link(previous)
                    for (frame, toggle), expected in zip(((0, 0), (47, 1), (92, 1)), outputs):
                        actual = render_noise(original, previous, frame, toggle)
                        assert max(abs(a - b) for a, b in zip(actual, expected)) <= 1
                    delete_program(original)
            delete_program(program)
        print(f"PASS: OpenGL {version}, GLSL {language}: three terminal shader programs compile/link; noise renders")
        if mode == "modern":
            print("PASS: GLSL 1.20 and previous 1.30 noise output match within one color level")
        make_current(display, None, None, None)
    finally:
        if context:
            function(egl, "eglDestroyContext", U, P, P)(display, context)
        if surface:
            function(egl, "eglDestroySurface", U, P, P)(display, surface)
        function(egl, "eglTerminate", U, P)(display)


if __name__ == "__main__":
    if len(sys.argv) == 1:
        for mode, version, language in (("legacy", "2.1", "120"), ("modern", "4.5", "450")):
            environment = dict(os.environ, EGL_PLATFORM="surfaceless", LIBGL_ALWAYS_SOFTWARE="1",
                               MESA_GL_VERSION_OVERRIDE=version, MESA_GLSL_VERSION_OVERRIDE=language)
            subprocess.run([sys.executable, str(Path(__file__).resolve()), mode], env=environment, check=True)
    else:
        assert sys.argv[1] in ("legacy", "modern")
        check_context(sys.argv[1])
