//===- TypeTranslation.cpp - type translation between MLIR LLVM & LLVM IR -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Target/LLVMIR/TypeTranslation.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/IR/MLIRContext.h"

#include "llvm/ADT/TypeSwitch.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"

using namespace mlir;

namespace mlir {
namespace LLVM {
namespace detail {
/// Support for translating MLIR LLVM dialect types to LLVM IR.
class TypeToLLVMIRTranslatorImpl {
public:
  /// Constructs a class creating types in the given LLVM context.
  TypeToLLVMIRTranslatorImpl(llvm::LLVMContext &context) : context(context) {}

  /// Translates a single type.
  llvm::Type *translateType(LLVM::LLVMType type) {
    // If the conversion is already known, just return it.
    if (knownTranslations.count(type))
      return knownTranslations.lookup(type);

    // Dispatch to an appropriate function.
    llvm::Type *translated =
        llvm::TypeSwitch<LLVM::LLVMType, llvm::Type *>(type)
            .Case([this](LLVM::LLVMVoidType) {
              return llvm::Type::getVoidTy(context);
            })
            .Case([this](LLVM::LLVMHalfType) {
              return llvm::Type::getHalfTy(context);
            })
            .Case([this](LLVM::LLVMBFloatType) {
              return llvm::Type::getBFloatTy(context);
            })
            .Case([this](LLVM::LLVMFloatType) {
              return llvm::Type::getFloatTy(context);
            })
            .Case([this](LLVM::LLVMDoubleType) {
              return llvm::Type::getDoubleTy(context);
            })
            .Case([this](LLVM::LLVMFP128Type) {
              return llvm::Type::getFP128Ty(context);
            })
            .Case([this](LLVM::LLVMX86FP80Type) {
              return llvm::Type::getX86_FP80Ty(context);
            })
            .Case([this](LLVM::LLVMPPCFP128Type) {
              return llvm::Type::getPPC_FP128Ty(context);
            })
            .Case([this](LLVM::LLVMX86MMXType) {
              return llvm::Type::getX86_MMXTy(context);
            })
            .Case([this](LLVM::LLVMTokenType) {
              return llvm::Type::getTokenTy(context);
            })
            .Case([this](LLVM::LLVMLabelType) {
              return llvm::Type::getLabelTy(context);
            })
            .Case([this](LLVM::LLVMMetadataType) {
              return llvm::Type::getMetadataTy(context);
            })
            .Case<LLVM::LLVMArrayType, LLVM::LLVMIntegerType,
                  LLVM::LLVMFunctionType, LLVM::LLVMPointerType,
                  LLVM::LLVMStructType, LLVM::LLVMFixedVectorType,
                  LLVM::LLVMScalableVectorType>(
                [this](auto type) { return this->translate(type); })
            .Default([](LLVM::LLVMType t) -> llvm::Type * {
              llvm_unreachable("unknown LLVM dialect type");
            });

    // Cache the result of the conversion and return.
    knownTranslations.try_emplace(type, translated);
    return translated;
  }

private:
  /// Translates the given array type.
  llvm::Type *translate(LLVM::LLVMArrayType type) {
    return llvm::ArrayType::get(translateType(type.getElementType()),
                                type.getNumElements());
  }

  /// Translates the given function type.
  llvm::Type *translate(LLVM::LLVMFunctionType type) {
    SmallVector<llvm::Type *, 8> paramTypes;
    translateTypes(type.getParams(), paramTypes);
    return llvm::FunctionType::get(translateType(type.getReturnType()),
                                   paramTypes, type.isVarArg());
  }

  /// Translates the given integer type.
  llvm::Type *translate(LLVM::LLVMIntegerType type) {
    return llvm::IntegerType::get(context, type.getBitWidth());
  }

  /// Translates the given pointer type.
  llvm::Type *translate(LLVM::LLVMPointerType type) {
    return llvm::PointerType::get(translateType(type.getElementType()),
                                  type.getAddressSpace());
  }

  /// Translates the given structure type, supports both identified and literal
  /// structs. This will _create_ a new identified structure every time, use
  /// `convertType` if a structure with the same name must be looked up instead.
  llvm::Type *translate(LLVM::LLVMStructType type) {
    SmallVector<llvm::Type *, 8> subtypes;
    if (!type.isIdentified()) {
      translateTypes(type.getBody(), subtypes);
      return llvm::StructType::get(context, subtypes, type.isPacked());
    }

    llvm::StructType *structType =
        llvm::StructType::create(context, type.getName());
    // Mark the type we just created as known so that recursive calls can pick
    // it up and use directly.
    knownTranslations.try_emplace(type, structType);
    if (type.isOpaque())
      return structType;

    translateTypes(type.getBody(), subtypes);
    structType->setBody(subtypes, type.isPacked());
    return structType;
  }

  /// Translates the given fixed-vector type.
  llvm::Type *translate(LLVM::LLVMFixedVectorType type) {
    return llvm::FixedVectorType::get(translateType(type.getElementType()),
                                      type.getNumElements());
  }

  /// Translates the given scalable-vector type.
  llvm::Type *translate(LLVM::LLVMScalableVectorType type) {
    return llvm::ScalableVectorType::get(translateType(type.getElementType()),
                                         type.getMinNumElements());
  }

  /// Translates a list of types.
  void translateTypes(ArrayRef<LLVM::LLVMType> types,
                      SmallVectorImpl<llvm::Type *> &result) {
    result.reserve(result.size() + types.size());
    for (auto type : types)
      result.push_back(translateType(type));
  }

  /// Reference to the context in which the LLVM IR types are created.
  llvm::LLVMContext &context;

  /// Map of known translation. This serves a double purpose: caches translation
  /// results to avoid repeated recursive calls and makes sure identified
  /// structs with the same name (that is, equal) are resolved to an existing
  /// type instead of creating a new type.
  llvm::DenseMap<LLVM::LLVMType, llvm::Type *> knownTranslations;
};
} // end namespace detail
} // end namespace LLVM
} // end namespace mlir

LLVM::TypeToLLVMIRTranslator::TypeToLLVMIRTranslator(llvm::LLVMContext &context)
    : impl(new detail::TypeToLLVMIRTranslatorImpl(context)) {}

LLVM::TypeToLLVMIRTranslator::~TypeToLLVMIRTranslator() {}

llvm::Type *LLVM::TypeToLLVMIRTranslator::translateType(LLVM::LLVMType type) {
  return impl->translateType(type);
}

unsigned LLVM::TypeToLLVMIRTranslator::getPreferredAlignment(
    LLVM::LLVMType type, const llvm::DataLayout &layout) {
  return layout.getPrefTypeAlignment(translateType(type));
}

namespace mlir {
namespace LLVM {
namespace detail {
/// Support for translating LLVM IR types to MLIR LLVM dialect types.
class TypeFromLLVMIRTranslatorImpl {
public:
  /// Constructs a class creating types in the given MLIR context.
  TypeFromLLVMIRTranslatorImpl(MLIRContext &context) : context(context) {}

  /// Translates the given type.
  LLVM::LLVMType translateType(llvm::Type *type) {
    if (knownTranslations.count(type))
      return knownTranslations.lookup(type);

    LLVM::LLVMType translated =
        llvm::TypeSwitch<llvm::Type *, LLVM::LLVMType>(type)
            .Case<llvm::ArrayType, llvm::FunctionType, llvm::IntegerType,
                  llvm::PointerType, llvm::StructType, llvm::FixedVectorType,
                  llvm::ScalableVectorType>(
                [this](auto *type) { return this->translate(type); })
            .Default([this](llvm::Type *type) {
              return translatePrimitiveType(type);
            });
    knownTranslations.try_emplace(type, translated);
    return translated;
  }

private:
  /// Translates the given primitive, i.e. non-parametric in MLIR nomenclature,
  /// type.
  LLVM::LLVMType translatePrimitiveType(llvm::Type *type) {
    if (type->isVoidTy())
      return LLVM::LLVMVoidType::get(&context);
    if (type->isHalfTy())
      return LLVM::LLVMHalfType::get(&context);
    if (type->isBFloatTy())
      return LLVM::LLVMBFloatType::get(&context);
    if (type->isFloatTy())
      return LLVM::LLVMFloatType::get(&context);
    if (type->isDoubleTy())
      return LLVM::LLVMDoubleType::get(&context);
    if (type->isFP128Ty())
      return LLVM::LLVMFP128Type::get(&context);
    if (type->isX86_FP80Ty())
      return LLVM::LLVMX86FP80Type::get(&context);
    if (type->isPPC_FP128Ty())
      return LLVM::LLVMPPCFP128Type::get(&context);
    if (type->isX86_MMXTy())
      return LLVM::LLVMX86MMXType::get(&context);
    if (type->isLabelTy())
      return LLVM::LLVMLabelType::get(&context);
    if (type->isMetadataTy())
      return LLVM::LLVMMetadataType::get(&context);
    llvm_unreachable("not a primitive type");
  }

  /// Translates the given array type.
  LLVM::LLVMType translate(llvm::ArrayType *type) {
    return LLVM::LLVMArrayType::get(translateType(type->getElementType()),
                                    type->getNumElements());
  }

  /// Translates the given function type.
  LLVM::LLVMType translate(llvm::FunctionType *type) {
    SmallVector<LLVM::LLVMType, 8> paramTypes;
    translateTypes(type->params(), paramTypes);
    return LLVM::LLVMFunctionType::get(translateType(type->getReturnType()),
                                       paramTypes, type->isVarArg());
  }

  /// Translates the given integer type.
  LLVM::LLVMType translate(llvm::IntegerType *type) {
    return LLVM::LLVMIntegerType::get(&context, type->getBitWidth());
  }

  /// Translates the given pointer type.
  LLVM::LLVMType translate(llvm::PointerType *type) {
    return LLVM::LLVMPointerType::get(translateType(type->getElementType()),
                                      type->getAddressSpace());
  }

  /// Translates the given structure type.
  LLVM::LLVMType translate(llvm::StructType *type) {
    SmallVector<LLVM::LLVMType, 8> subtypes;
    if (type->isLiteral()) {
      translateTypes(type->subtypes(), subtypes);
      return LLVM::LLVMStructType::getLiteral(&context, subtypes,
                                              type->isPacked());
    }

    if (type->isOpaque())
      return LLVM::LLVMStructType::getOpaque(type->getName(), &context);

    LLVM::LLVMStructType translated =
        LLVM::LLVMStructType::getIdentified(&context, type->getName());
    knownTranslations.try_emplace(type, translated);
    translateTypes(type->subtypes(), subtypes);
    LogicalResult bodySet = translated.setBody(subtypes, type->isPacked());
    assert(succeeded(bodySet) &&
           "could not set the body of an identified struct");
    (void)bodySet;
    return translated;
  }

  /// Translates the given fixed-vector type.
  LLVM::LLVMType translate(llvm::FixedVectorType *type) {
    return LLVM::LLVMFixedVectorType::get(translateType(type->getElementType()),
                                          type->getNumElements());
  }

  /// Translates the given scalable-vector type.
  LLVM::LLVMType translate(llvm::ScalableVectorType *type) {
    return LLVM::LLVMScalableVectorType::get(
        translateType(type->getElementType()), type->getMinNumElements());
  }

  /// Translates a list of types.
  void translateTypes(ArrayRef<llvm::Type *> types,
                      SmallVectorImpl<LLVM::LLVMType> &result) {
    result.reserve(result.size() + types.size());
    for (llvm::Type *type : types)
      result.push_back(translateType(type));
  }

  /// Map of known translations. Serves as a cache and as recursion stopper for
  /// translating recursive structs.
  llvm::DenseMap<llvm::Type *, LLVM::LLVMType> knownTranslations;

  /// The context in which MLIR types are created.
  MLIRContext &context;
};
} // end namespace detail
} // end namespace LLVM
} // end namespace mlir

LLVM::TypeFromLLVMIRTranslator::TypeFromLLVMIRTranslator(MLIRContext &context)
    : impl(new detail::TypeFromLLVMIRTranslatorImpl(context)) {}

LLVM::TypeFromLLVMIRTranslator::~TypeFromLLVMIRTranslator() {}

LLVM::LLVMType LLVM::TypeFromLLVMIRTranslator::translateType(llvm::Type *type) {
  return impl->translateType(type);
}
