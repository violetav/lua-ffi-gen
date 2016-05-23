#include "GenerateFFIBindings.hpp"

void RecordVisitor::setOutput(std::string *output_) { output = output_; }

void RecordVisitor::checkFieldType(QualType FieldType, bool *isResolved,
                                   std::vector<std::string> *dependencyList,
                                   std::string &RecordDeclaration) {

  unsigned Qualifiers = FieldType.getLocalCVRQualifiers();
  QualType FieldTypeFull = FieldType;
  FieldType.removeLocalCVRQualifiers(Qualifiers);

  if (const TypedefType *TT = FieldType->getAs<TypedefType>()) {

    if (FFIBindingsUtils::getInstance()->isOnBlacklist(
            FieldType.getAsString())) {
      FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
          "typedef " + FieldType.getAsString());
    } else {

      TypedefNameDecl *TD = TT->getDecl();

      TypeDeclaration TypedefTypeDeclaration;
      TypedefTypeDeclaration.Declaration = TD;
      TypedefTypeDeclaration.TypeName = "typedef " + FieldType.getAsString();
      *isResolved = false;
      dependencyList->push_back("typedef " + FieldType.getAsString());

      if (FFIBindingsUtils::getInstance()->isNewType(FieldType)) {
        FFIBindingsUtils::getInstance()->getDeclsToFind()->push(
            TypedefTypeDeclaration);
      }
    }

    RecordDeclaration = FieldTypeFull.getAsString() + " " + RecordDeclaration;

  } else if (const RecordType *RT = FieldType->getAs<RecordType>()) {
    RecordDecl *RD = RT->getDecl();

    if (FFIBindingsUtils::getInstance()->isOnBlacklist(
            FieldType.getAsString())) {
      FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
          RD->getTypeForDecl()->getCanonicalTypeInternal().getAsString());
      RecordDeclaration = FieldTypeFull.getAsString() + " " + RecordDeclaration;
    } else {

      TypeDeclaration RecordTypeDeclaration;
      RecordTypeDeclaration.Declaration = RD;
      RecordTypeDeclaration.TypeName =
          RD->getTypeForDecl()->getCanonicalTypeInternal().getAsString();

      // record with no name should be resolved, but not printed by itself (it
      // should be printed with this declaration)
      if (RD->getNameAsString() == "") {
        std::string AnonRecordDeclaration;

        if (FieldType->isStructureType()) {
          AnonRecordDeclaration += "struct ";
        } else if (FieldType->isUnionType()) {
          AnonRecordDeclaration += "union ";
        }

        AnonRecordDeclaration +=
            FFIBindingsUtils::getInstance()->getDeclAttrs(RD);
        AnonRecordDeclaration += "{\n";

        // check the fields
        for (RecordDecl::field_iterator FI = RD->field_begin();
             FI != RD->field_end(); ++FI) {
          std::string FieldDeclaration = FI->getNameAsString();
          checkFieldType(FI->getType(), isResolved, dependencyList,
                         FieldDeclaration);
          AnonRecordDeclaration += FieldDeclaration + ";\n";
        }
        AnonRecordDeclaration += "}";

        if (!RD->isAnonymousStructOrUnion()) {
          AnonRecordDeclaration += " ";
        }

        RecordDeclaration = AnonRecordDeclaration + RecordDeclaration;
      } else {
        RecordDeclaration =
            FieldTypeFull.getAsString() + " " + RecordDeclaration;
        dependencyList->push_back(
            RD->getTypeForDecl()->getCanonicalTypeInternal().getAsString());

        *isResolved = false;
        if (FFIBindingsUtils::getInstance()->isNewType(FieldType)) {
          FFIBindingsUtils::getInstance()->getDeclsToFind()->push(
              RecordTypeDeclaration);
        }
      }
    }

  } else if (const EnumType *ET = FieldType->getAs<EnumType>()) {

    if (FFIBindingsUtils::getInstance()->isOnBlacklist(
            FieldType.getAsString())) {
      FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
          FieldType.getAsString());
      RecordDeclaration = FieldTypeFull.getAsString() + " " + RecordDeclaration;
    } else {
      EnumDecl *ED = ET->getDecl();

      if (ED->getNameAsString() == "") {
        std::vector<std::string> elements;
        std::string AnonEnumDeclaration;

        std::string attrs = FFIBindingsUtils::getInstance()->getDeclAttrs(ED);
        AnonEnumDeclaration += "enum " + attrs + "{";

        int length = 0;
        for (EnumDecl::enumerator_iterator EI = ED->enumerator_begin();
             EI != ED->enumerator_end(); ++EI) {
          elements.push_back(EI->getNameAsString());
          length++;
        }

        for (int i = 0; i < length; i++) {
          if (i < length - 1) {
            AnonEnumDeclaration += elements[i] + ", ";
          } else {
            AnonEnumDeclaration += elements[i] + "} ";
          }
        }
        RecordDeclaration = AnonEnumDeclaration + RecordDeclaration;
      } else {
        TypeDeclaration EnumTypeDeclaration;
        EnumTypeDeclaration.Declaration = ED;
        EnumTypeDeclaration.TypeName = FieldType.getAsString();

        RecordDeclaration =
            FieldTypeFull.getAsString() + " " + RecordDeclaration;
        *isResolved = false;
        dependencyList->push_back(FieldType.getAsString());

        if (FFIBindingsUtils::getInstance()->isNewType(FieldType)) {
          FFIBindingsUtils::getInstance()->getDeclsToFind()->push(
              EnumTypeDeclaration);
        }
      }
    }

  } else if (FieldType->isFundamentalType()) {
    RecordDeclaration = FieldTypeFull.getAsString() + " " + RecordDeclaration;
  } else if (FieldType->isFunctionPointerType()) {
    std::string FunctionPointerDeclarationCore;
    const FunctionProtoType *FPT =
        (const FunctionProtoType *)
        FieldType->getPointeeType()->getAs<FunctionType>();
    std::string ReturnValueDeclaration;
    FunctionVisitor::checkParameterType(FPT->getReturnType(), isResolved,
                                        dependencyList, ReturnValueDeclaration,
                                        FunctionVisitor::RETVAL);
    RecordDeclaration =
        ReturnValueDeclaration + " (*" + RecordDeclaration + ")" + "(";
    unsigned int NumOfParams = FPT->getNumParams();
    for (unsigned int i = 0; i < NumOfParams; i++) {
      std::string ParameterDeclaration;
      FunctionVisitor::checkParameterType(FPT->getParamType(i), isResolved,
                                          dependencyList, ParameterDeclaration,
                                          FunctionVisitor::PARAM);
      FunctionPointerDeclarationCore += ParameterDeclaration;
      if (i != NumOfParams - 1) {
        FunctionPointerDeclarationCore += ", ";
      }
    }
    if (FPT->isVariadic()) {
      FunctionPointerDeclarationCore += ", ...";
    }
    FunctionPointerDeclarationCore += ")";
    RecordDeclaration += FunctionPointerDeclarationCore;
  } else if (FieldType->isPointerType()) {
    if (FieldType->getPointeeType()->isFundamentalType() &&
        !(FieldType->getPointeeType()->getAs<TypedefType>())) {
      RecordDeclaration = FieldTypeFull.getAsString() + " " + RecordDeclaration;
    } else {
      RecordDeclaration = "(*" + RecordDeclaration + ")";
      checkFieldType(FieldType->getPointeeType(), isResolved, dependencyList,
                     RecordDeclaration);
    }
  } else if (FieldType->isArrayType()) {
    std::string ArrayDeclaration;
    QualType ElementType;
    if (FieldType->isConstantArrayType()) {
      ArrayDeclaration = FFIBindingsUtils::getInstance()->getArraySize(
          Context->getAsConstantArrayType(FieldType));
      ElementType =
          Context->getAsConstantArrayType(FieldType)->getElementType();
    } else if (FieldType->isIncompleteArrayType()) {
      ArrayDeclaration = "[]";
      ElementType =
          Context->getAsIncompleteArrayType(FieldType)->getElementType();
    }
    RecordDeclaration += ArrayDeclaration;
    checkFieldType(ElementType, isResolved, dependencyList, RecordDeclaration);
  } else if (const VectorType *VT = FieldType->getAs<VectorType>()) {
    if (VT->getVectorKind() == VectorType::GenericVector &&
        VT->getElementType()->isFundamentalType()) {
      RecordDeclaration = VT->getElementType().getAsString() + " " +
                          RecordDeclaration +
                          " __attribute__((__vector_size__(" +
                          std::to_string(VT->getNumElements()) + " * sizeof(" +
                          VT->getElementType().getAsString() + "))));\n";
    }
  }
}

bool RecordVisitor::VisitRecordDecl(RecordDecl *RD) {

  // try to resolve record declaration if it has ffibinding attribute
  if (RD->hasAttr<FFIBindingAttr>()) {
    FFIBindingsUtils::getInstance()->setHasMarkedDeclarations(true);
    // if this record type has already been resolved, then there's nothing to do
    if (!FFIBindingsUtils::getInstance()->isNewType(
             RD->getTypeForDecl()->getCanonicalTypeInternal())) {
      return true;
    }
    findRecordDeclaration(RD);
  }
  return true;
}

void RecordVisitor::findRecordDeclaration(RecordDecl *RD) {

  bool isResolved = true;
  std::string RecordDeclaration;
  std::vector<std::string> *dependencyList = new std::vector<std::string>();

  std::string attrList = FFIBindingsUtils::getInstance()->getDeclAttrs(RD);

  if (RD->getTypeForDecl()->isStructureType()) {
    RecordDeclaration = "struct ";
  } else if (RD->getTypeForDecl()->isUnionType()) {
    RecordDeclaration = "union ";
  }

  RecordDeclaration += attrList + RD->getNameAsString() + " {\n";

  // check the fields
  for (RecordDecl::field_iterator FI = RD->field_begin(); FI != RD->field_end();
       ++FI) {
    if (FI->isBitField()) {
      RecordDeclaration +=
          FI->getType().getAsString() + " " + FI->getNameAsString() + " : " +
          std::to_string(FI->getBitWidthValue(*Context)) + ";\n";
      continue;
    }
    std::string FieldDeclaration = FI->getNameAsString();
    checkFieldType(FI->getType(), &isResolved, dependencyList,
                   FieldDeclaration);
    RecordDeclaration += FieldDeclaration + ";\n";
  }
  RecordDeclaration += "};\n";
  if (isResolved) {
    FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
        RD->getTypeForDecl()->getCanonicalTypeInternal().getAsString());
    if (RD->field_empty()) {
      (*output) +=
          RD->getTypeForDecl()->getCanonicalTypeInternal().getAsString() +
          ";\n";
    } else {
      (*output) += RecordDeclaration;
    }
    (*output) += "\n";
    delete dependencyList;
  } else {
    // add this record to list of unresolved declarations
    DeclarationInfo RecordDeclarationInfo;
    RecordDeclarationInfo.isResolved = isResolved;
    RecordDeclarationInfo.dependencyList = dependencyList;
    RecordDeclarationInfo.Declaration = RecordDeclaration;

    std::pair<std::string, DeclarationInfo> Record(
        RD->getTypeForDecl()->getCanonicalTypeInternal().getAsString(),
        RecordDeclarationInfo);
    FFIBindingsUtils::getInstance()->getUnresolvedDeclarations()->insert(
        Record);
  }
}
