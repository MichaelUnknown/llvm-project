; RUN: not llvm-as -disable-output <%s 2>&1 | FileCheck %s

!named = !{!0}
!0 = !DICompositeType(tag: DW_TAG_structure_type, name: "A", size: 64, allocated: !DIExpression(DW_OP_constu, 6789))

; CHECK:  allocated can only appear in array type
