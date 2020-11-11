    org 0x400

    loadimm.upper 0x20
    loadimm.lower 0x00
    mov r0 r7
    loadimm.upper 0x00
    loadimm.lower 0x02
    mov r1 r7
    
    mul r0 r0 r1
    brr.o Fail

    mul r0 r0 r1
    brr.o OverflowPos
    brr Fail

OverflowPos:
    test r7
    brr.z OverflowPosSucceed
    brr Fail

OverflowPosSucceed:
    mul r0 r0 r1
    brr.o OverflowNeg
    brr Fail

OverflowNeg:
    mov r0 r7
    loadimm.upper 0xff
    loadimm.lower 0xff
    sub r0 r0 r7
    brr.z Victory
    brr Fail

Fail:
    brr Fail

Victory:
    brr Victory
