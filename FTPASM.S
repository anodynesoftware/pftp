;
; pftp: assembler routines
;
; Copyright (c) 2013 Roger Burrows
;
; This file is distributed under the GPL, version 2 or at your
; option any later version.  See LICENSE.TXT for details.
;

        xdef	_getwh

        text
_getwh:
        dc.w	$a000					; lineA init
        move.l	-44(a0),d0				; v_cel_mx/v_cel_my
        rts

		end
