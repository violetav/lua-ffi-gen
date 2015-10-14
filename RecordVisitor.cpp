#include "GenerateFFIBindings.hpp"

void RecordVisitor::setOutput(llvm::raw_fd_ostream *output_) {
  output = output_;
}

void RecordVisitor::checkFieldType(QualType FieldType, std::string FieldName,
                                   bool *isResolved,
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
      TypedefTypeDeclaration.shouldBeResolved = false;
      TypedefTypeDeclaration.TypeName = "typedef " + FieldType.getAsString();

      *isResolved = false;
      dependencyList->push_back("typedef " + FieldType.getAsString());

      if (FFIBindingsUtils::getInstance()->isNewType(FieldType)) {
        FFIBindingsUtils::getInstance()->getDeclsToFind()->push(
            TypedefTypeDeclaration);
      }
    }

    RecordDeclaration += FieldTypeFull.getAsString() + " " + FieldName + ";\n";

  } else if (const RecordType *RT = FieldType->getAs<RecordType>()) {
    RecordDecl *RD = RT->getDecl();

    if (FFIBindingsUtils::getInstance()->isOnBlacklist(
            FieldType.getAsString())) {
      FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
          RD->getTypeForDecl()->getCanonicalTypeInternal().getAsString());
      RecordDeclaration +=
          FieldTypeFull.getAsString() + " " + FieldName + ";\n";
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
          AnonRecordDeclaration += "struct {\n";
        } else if (FieldType->isUnionType()) {
          AnonRecordDeclaration += "union {\n";
        }

        // check the fields
        for (RecordDecl::field_iterator FI = RD->field_begin();
             FI != RD->field_end(); ++FI) {
          checkFieldType(FI->getType(), FI->getNameAsString(), isResolved,
                         dependencyList, AnonRecordDeclaration);
        }
        AnonRecordDeclaration += "}";

        if (!RD->isAnonymousStructOrUnion()) {
          AnonRecordDeclaration += " " + FieldName;
        }
        AnonRecordDeclaration += ";\n";

        RecordDeclaration += AnonRecordDeclaration;
      } else {
        RecordDeclaration +=
            FieldTypeFull.getAsString() + " " + FieldName + ";\n";
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
      RecordDeclaration +=
          FieldTypeFull.getAsString() + " " + FieldName + ";\n";
    } else {
      EnumDecl *ED = ET->getDecl();

      if (ED->getNameAsString() == "") {
        std::vector<std::string> elements;

        RecordDeclaration += "enum {";

        int length = 0;
        for (EnumDecl::enumerator_iterator EI = ED->enumerator_begin();
             EI != ED->enumerator_end(); ++EI) {
          elements.push_back(EI->getNameAsString());
          length++;
        }

        for (int i = 0; i < length; i++) {
          if (i < length - 1) {
            RecordDeclaration += elements[i] + ", ";
          } else {
            RecordDeclaration += elements[i] + "} ";
          }
        }
        RecordDeclaration += FieldName + ";\n";
      } else {
        TypeDeclaration EnumTypeDeclaration;
        EnumTypeDeclaration.Declaration = ED;
        EnumTypeDeclaration.TypeName = FieldType.getAsString();

        RecordDeclaration +=
            FieldTypeFull.getAsString() + " " + FieldName + ";\n";
        *isResolved = false;
        dependencyList->push_back(FieldType.getAsString());

        if (FFIBindingsUtils::getInstance()->isNewType(FieldType)) {
          FFIBindingsUtils::getInstance()->getDeclsToFind()->push(
              EnumTypeDeclaration);
        }
      }
    }

  } else if (FieldType->isFundamentalType()) {
    RecordDeclaration += FieldTypeFull.getAsString() + " " + FieldName + ";\n";
  } else if (FieldType->isPointerType()) {
    if (FieldType->getPointeeType()->isFundamentalType() &&
        !(FieldType->getPointeeType()->getAs<TypedefType>())) {
      RecordDeclaration +=
          FieldTypeFull.getAsString() + " " + FieldName + ";\n";
    } else {
      RecordDeclaration += "void* " + FieldName + ";\n";
    }

  } else if (FieldType->isArrayType()) {

    std::string ArrayDeclaration;

    if (FieldType->isConstantArrayType()) {
      const ConstantArrayType *CAT = Context->getAsConstantArrayType(FieldType);
      ArrayDeclaration = FFIBindingsUtils::getInstance()->getArraySize(CAT);

      // e.g. typedef int* INT_PTR; INT_PTR is a typedeftype and also a
      // pointertype, so this has to be checked first
      if (CAT->getElementType()->getAs<TypedefType>()) {
        std::string TempArrayDeclaration;
        checkFieldType(CAT->getElementType(), FieldName, isResolved,
                       dependencyList, TempArrayDeclaration);
        RecordDeclaration += CAT->getElementType().getAsString() + " " +
                             FieldName + ArrayDeclaration + ";\n";
      } else if (CAT->getElementType()->isPointerType()) {
        if (CAT->getElementType()->getPointeeType()->isFundamentalType() &&
            !(CAT->getElementType()->getPointeeType()->getAs<TypedefType>())) {
          RecordDeclaration += CAT->getElementType().getAsString() + " " +
                               FieldName + ArrayDeclaration + ";\n";
        } else {
          RecordDeclaration += "void* " + FieldName + ArrayDeclaration + ";\n";
        }
      } else {
        std::string TempArrayDeclaration;
        checkFieldType(CAT->getElementType(), FieldName, isResolved,
                       dependencyList, TempArrayDeclaration);
        if (CAT->getElementType()->isArrayType()) {
          TempArrayDeclaration = "";
          QualType elementType =
              FFIBindingsUtils::getInstance()->handleMultiDimArrayType(
                  Context, CAT->getElementType(), TempArrayDeclaration);

          if (elementType->isPointerType()) {
            if (elementType->getPointeeType()->isFundamentalType() &&
                !(elementType->getPointeeType()->getAs<TypedefType>())) {
              RecordDeclaration += elementType.getAsString() + " " + FieldName +
                                   ArrayDeclaration + TempArrayDeclaration +
                                   ";\n";
            } else {
              RecordDeclaration += "void* " + FieldName + ArrayDeclaration +
                                   TempArrayDeclaration + ";\n";
            }

          } else {
            RecordDeclaration += elementType.getAsString() + " " + FieldName +
                                 ArrayDeclaration + TempArrayDeclaration +
                                 ";\n";
          }

        } else {
          RecordDeclaration += CAT->getElementType().getAsString() + " " +
                               FieldName + ArrayDeclaration + ";\n";
        }
      }
    }
  } else if (const VectorType *VT = FieldType->getAs<VectorType>()) {
    if (VT->getVectorKind() == VectorType::GenericVector &&
        VT->getElementType()->isFundamentalType()) {
      RecordDeclaration += VT->getElementType().getAsString() + " " +
                           FieldName + " __attribute__((__vector_size__(" +
                           std::to_string(VT->getNumElements()) + " * sizeof(" +
                           VT->getElementType().getAsString() + "))));\n";
    }
  }
}

bool RecordVisitor::VisitRecordDecl(RecordDecl *RD) {

  // try to resolve record declaration if it has ffibinding attribute
  if (RD->hasAttr<FFIBindingAttr>()) {
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

  RecordDeclaration =
      RD->getTypeForDecl()->getCanonicalTypeInternal().getAsString() + " {\n";
  // check the fields
  for (RecordDecl::field_iterator FI = RD->field_begin(); FI != RD->field_end();
       ++FI) {
    checkFieldType(FI->getType(), FI->getNameAsString(), &isResolved,
                   dependencyList, RecordDeclaration);
  }
  RecordDeclaration += "};\n";
  if (isResolved) {
    FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
        RD->getTypeForDecl()->getCanonicalTypeInternal().getAsString());
    (*output) << RecordDeclaration;
    (*output) << "\n";
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
