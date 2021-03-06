add_mlir_library(MLIRIR
  AffineExpr.cpp
  AffineMap.cpp
  AsmPrinter.cpp
  Attributes.cpp
  Block.cpp
  Builders.cpp
  BuiltinAttributes.cpp
  BuiltinDialect.cpp
  BuiltinTypes.cpp
  Diagnostics.cpp
  Dialect.cpp
  Dominance.cpp
  FunctionImplementation.cpp
  FunctionSupport.cpp
  IntegerSet.cpp
  Location.cpp
  MLIRContext.cpp
  Operation.cpp
  OperationSupport.cpp
  PatternMatch.cpp
  Region.cpp
  RegionKindInterface.cpp
  SymbolTable.cpp
  Types.cpp
  TypeRange.cpp
  TypeUtilities.cpp
  Value.cpp
  Verifier.cpp
  Visitors.cpp

  ADDITIONAL_HEADER_DIRS
  ${MLIR_MAIN_INCLUDE_DIR}/mlir/IR

  DEPENDS
  MLIRBuiltinOpsIncGen
  MLIRCallInterfacesIncGen
  MLIROpAsmInterfaceIncGen
  MLIRRegionKindInterfaceIncGen
  MLIRSymbolInterfacesIncGen

  LINK_LIBS PUBLIC
  MLIRSupport
  )
