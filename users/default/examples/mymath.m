#====================================================================
# mymath.m
#
# Description:
# 
#  This cmath script demonstrates the capabilites of cmath powered 
#  by _CALC math engine.
#
#  Run the script like this:
#
#    cmath mymath.m 10 2
#    5, 50 
#
#  i.e. cmath is able to take your input values, run the script and
#  return the results back to commandline in stdout.
#
#====================================================================

# Note! to enable echos, remove " and ;

# print ""
# print "This is example math application:"

U = $input1;
R = $input2;

# print "calculating current and power..."

I = U / R;
P = U * I;

# print "outputting both as array"

OUT = [I,P];

return OUT

