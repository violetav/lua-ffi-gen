#include "GenerateFFIBindings.hpp"

FFIBindingsUtils *FFIBindingsUtils::instance = NULL;

FFIBindingsUtils *FFIBindingsUtils::getInstance() {

  if (!instance) {
    instance = new FFIBindingsUtils();
  }
  return instance;
}

bool FFIBindingsUtils::isNewType(QualType Type) {

  if (isInResolvedDecls(Type.getAsString())) {
    return false;
  }
  if (isInUnresolvedDeclarations(Type.getAsString())) {
    return false;
  }
  return true;
}

bool FFIBindingsUtils::isInUnresolvedDeclarations(std::string DeclType) {

  if (UnresolvedDeclarations->find(DeclType) != UnresolvedDeclarations->end()) {
    return true;
  }
  return false;
}

bool FFIBindingsUtils::isInResolvedDecls(std::string DeclType) {

  if (ResolvedDecls->find(DeclType) != ResolvedDecls->end()) {
    return true;
  }
  return false;
}

bool FFIBindingsUtils::isOnBlacklist(std::string DeclType) {

  if (blacklist->find(DeclType) != blacklist->end()) {
    return true;
  }
  return false;
}

std::string FFIBindingsUtils::getArraySize(const ConstantArrayType *CAT) {

  std::string ArraySize = "[";

  if (CAT->getIndexTypeQualifiers().hasQualifiers()) {
    unsigned TypeQuals = CAT->getIndexTypeCVRQualifiers();
    bool appendSpace = false;
    if (TypeQuals & Qualifiers::Const) {
      ArraySize += "const";
      appendSpace = true;
    }
    if (TypeQuals & Qualifiers::Volatile) {
      if (appendSpace) {
        ArraySize += ' ';
      }
      ArraySize += "volatile";
      appendSpace = true;
    }
    if (TypeQuals & Qualifiers::Restrict) {
      if (appendSpace) {
        ArraySize += ' ';
      }
      ArraySize += "restrict";
    }

    ArraySize += ' ';
  }

  ArraySize += std::to_string(CAT->getSize().getZExtValue()) + ']';
  return ArraySize;
}

QualType FFIBindingsUtils::handleMultiDimArrayType(
    ASTContext *Context, QualType ArrayType, std::string &ArrayDeclaration) {
  const ConstantArrayType *CAT = Context->getAsConstantArrayType(ArrayType);
  QualType ElementType = CAT->getElementType();

  std::string TempArrayDeclaration;

  TempArrayDeclaration = getArraySize(CAT);

  ArrayDeclaration += TempArrayDeclaration;
  if (CAT->getElementType()->isArrayType() &&
      !(CAT->getElementType()->getAs<TypedefType>())) {
    ElementType = handleMultiDimArrayType(Context, CAT->getElementType(),
                                          ArrayDeclaration);
  }

  return ElementType;
}
