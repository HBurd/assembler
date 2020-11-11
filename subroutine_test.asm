		LOADIMM.upper 0
		LOADIMM.lower 10
		BR.SUB R7 3
		NOP
DONE:	
                BR R6 0		; victory loop
		BR R0 0
		NOP
		NOP
		MOV R5 R7	; save PC
		LOADIMM.lower 8
		MOV R6 R7
		MOV R7 R5
		BRR DONE
		RETURN
