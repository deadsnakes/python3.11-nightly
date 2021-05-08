# This script generates the opcode.h header file.

import sys
import tokenize

header = """
/* Auto-generated by Tools/scripts/generate_opcode_h.py from Lib/opcode.py */
#ifndef Py_OPCODE_H
#define Py_OPCODE_H
#ifdef __cplusplus
extern "C" {
#endif


    /* Instruction opcodes for compiled code */
""".lstrip()

footer = """
#define HAS_ARG(op) ((op) >= HAVE_ARGUMENT)

/* Reserve some bytecodes for internal use in the compiler.
 * The value of 240 is arbitrary. */
#define IS_ARTIFICIAL(op) ((op) > 240)

#ifdef __cplusplus
}
#endif
#endif /* !Py_OPCODE_H */
"""

UINT32_MASK = (1<<32)-1

def write_int_array_from_ops(name, ops, out):
    bits = 0
    for op in ops:
        bits |= 1<<op
    out.write(f"static uint32_t {name}[8] = {{\n")
    for i in range(8):
        out.write(f"    {bits & UINT32_MASK}U,\n")
        bits >>= 32
    assert bits == 0
    out.write(f"}};\n")

def main(opcode_py, outfile='Include/opcode.h'):
    opcode = {}
    if hasattr(tokenize, 'open'):
        fp = tokenize.open(opcode_py)   # Python 3.2+
    else:
        fp = open(opcode_py)            # Python 2.7
    with fp:
        code = fp.read()
    exec(code, opcode)
    opmap = opcode['opmap']
    hasjrel = opcode['hasjrel']
    hasjabs = opcode['hasjabs']
    with open(outfile, 'w') as fobj:
        fobj.write(header)
        for name in opcode['opname']:
            if name in opmap:
                fobj.write("#define %-23s %3s\n" % (name, opmap[name]))
            if name == 'POP_EXCEPT': # Special entry for HAVE_ARGUMENT
                fobj.write("#define %-23s %3d\n" %
                            ('HAVE_ARGUMENT', opcode['HAVE_ARGUMENT']))
        fobj.write("#ifdef NEED_OPCODE_JUMP_TABLES\n")
        write_int_array_from_ops("_PyOpcode_RelativeJump", opcode['hasjrel'], fobj)
        write_int_array_from_ops("_PyOpcode_Jump", opcode['hasjrel'] + opcode['hasjabs'], fobj)
        fobj.write("#endif /* OPCODE_TABLES */\n")
        fobj.write(footer)


    print("%s regenerated from %s" % (outfile, opcode_py))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
