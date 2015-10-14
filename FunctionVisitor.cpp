#include "GenerateFFIBindings.hpp"

void FunctionVisitor::setOutput(llvm::raw_fd_ostream *output_) {
  output = output_;
}

void
FunctionVisitor::checkParameterType(QualType ParameterType, bool *isResolved,
                                    std::vector<std::string> *dependencyList,
                                    std::string &DeclarationCore,
                                    enum Type parameterType) {

  unsigned Qualifiers = ParameterType.getLocalCVRQualifiers();
  QualType ParamTypeFull = ParameterType;
  ParameterType.removeLocalCVRQualifiers(Qualifiers);

  // typedef type should be checked for first, because e.g. a typedef of a
  // structure is both typedef type and record type
  if (const TypedefType *TT = ParameterType->getAs<TypedefType>()) {
    TypedefNameDecl *TD = TT->getDecl();

    if (FFIBindingsUtils::getInstance()->isOnBlacklist(
            ParameterType.getAsString())) {
      FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
          "typedef " + ParameterType.getAsString());
    } else {
      TypeDeclaration TypedefTypeDeclaration;
      TypedefTypeDeclaration.Declaration = TD;
      TypedefTypeDeclaration.TypeName =
          "typedef " + ParameterType.getAsString();
      *isResolved = false;
      dependencyList->push_back("typedef " + ParameterType.getAsString());

      if (FFIBindingsUtils::getInstance()->isNewType(ParameterType)) {
        FFIBindingsUtils::getInstance()->getDeclsToFind()->push(
            TypedefTypeDeclaration);
      }
    }
    if (DeclarationCore == "") {
      DeclarationCore = ParamTypeFull.getAsString();
    } else {
      DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
    }
  } else if (const RecordType *RT = ParameterType->getAs<RecordType>()) {
    RecordDecl *RD = RT->getDecl();
    TypeDeclaration RecordTypeDeclaration;
    RecordTypeDeclaration.Declaration = RD;

    if (FFIBindingsUtils::getInstance()->isOnBlacklist(
            ParameterType.getAsString())) {
      FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
          ParameterType.getAsString());
      if (DeclarationCore == "") {
        DeclarationCore = ParamTypeFull.getAsString();
      } else {
        DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
      }
    } else {

      if (RD->getNameAsString() == "") {
        std::string AnonRecordName =
            RD->getTypeForDecl()->getCanonicalTypeInternal().getAsString();
        char separator;
#ifdef LLVM_ON_UNIX
        separator = '/';
#else
        separator = '\\';
#endif

        unsigned int j = 0;
        int distance;
        for (std::vector<std::string>::iterator i =
                 FFIBindingsUtils::getInstance()
                     ->getAnonymousRecords()
                     ->begin();
             i != FFIBindingsUtils::getInstance()->getAnonymousRecords()->end();
             ++i) {
          if (*i == AnonRecordName) {
            distance = std::distance(
                FFIBindingsUtils::getInstance()->getAnonymousRecords()->begin(),
                i);
            break;
          }
          j++;
        }
        if (j ==
            FFIBindingsUtils::getInstance()->getAnonymousRecords()->size()) {
          FFIBindingsUtils::getInstance()->getAnonymousRecords()->push_back(
              AnonRecordName);
          distance =
              FFIBindingsUtils::getInstance()->getAnonymousRecords()->size() -
              1;
        }
        int firstindex;
        if (AnonRecordName.find_last_of(separator) == std::string::npos) {
          firstindex = AnonRecordName.find("at ") + 2;
        } else {
          firstindex = AnonRecordName.find_last_of(separator);
        }

        AnonRecordName = AnonRecordName.substr(
            firstindex + 1, AnonRecordName.find_first_of(':') - firstindex - 1);
        std::replace(AnonRecordName.begin(), AnonRecordName.end(), '.', '_');
        AnonRecordName =
            "Anonymous_" + AnonRecordName + "_" + std::to_string(distance);

        if (RT->isStructureType()) {
          AnonRecordName = "struct " + AnonRecordName;
        } else if (RT->isUnionType()) {
          AnonRecordName = "union " + AnonRecordName;
        }

        *isResolved = false;

        RecordTypeDeclaration.TypeName = AnonRecordName;
        dependencyList->push_back(AnonRecordName);

        if (DeclarationCore == "") {
          DeclarationCore = AnonRecordName;
        } else {
          DeclarationCore = AnonRecordName + " " + DeclarationCore;
        }

        if (!FFIBindingsUtils::getInstance()->isInResolvedDecls(
                 AnonRecordName) &&
            !FFIBindingsUtils::getInstance()->isInUnresolvedDeclarations(
                 AnonRecordName)) {
          FFIBindingsUtils::getInstance()->getDeclsToFind()->push(
              RecordTypeDeclaration);
        }

      } else {
        RecordTypeDeclaration.TypeName = ParameterType.getAsString();
        *isResolved = false;
        dependencyList->push_back(ParameterType.getAsString());

        if (DeclarationCore == "") {
          DeclarationCore = ParamTypeFull.getAsString();
        } else {
          DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
        }

        if (FFIBindingsUtils::getInstance()->isNewType(ParameterType)) {
          FFIBindingsUtils::getInstance()->getDeclsToFind()->push(
              RecordTypeDeclaration);
        }
      }
    }

  } else if (const EnumType *ET = ParameterType->getAs<EnumType>()) {
    EnumDecl *ED = ET->getDecl();

    if (FFIBindingsUtils::getInstance()->isOnBlacklist(
            ParameterType.getAsString())) {
      FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
          ParameterType.getAsString());
      if (DeclarationCore == "") {
        DeclarationCore = ParamTypeFull.getAsString();
      } else {
        DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
      }
    } else {
      if (ED->getNameAsString() == "") {
        std::vector<std::string> elements;
        std::string EnumDeclaration;

        EnumDeclaration += "enum {";

        int length = 0;
        for (EnumDecl::enumerator_iterator EI = ED->enumerator_begin();
             EI != ED->enumerator_end(); ++EI) {
          elements.push_back(EI->getNameAsString());
          length++;
        }

        for (int i = 0; i < length; i++) {
          if (i < length - 1) {
            EnumDeclaration += elements[i] + ", ";
          } else {
            EnumDeclaration += elements[i] + "} ";
          }
        }
        DeclarationCore = EnumDeclaration + DeclarationCore;
      } else {
        TypeDeclaration EnumTypeDeclaration;
        EnumTypeDeclaration.Declaration = ED;
        EnumTypeDeclaration.TypeName = ParameterType.getAsString();

        *isResolved = false;
        dependencyList->push_back(ParameterType.getAsString());

        if (FFIBindingsUtils::getInstance()->isNewType(ParameterType)) {
          FFIBindingsUtils::getInstance()->getDeclsToFind()->push(
              EnumTypeDeclaration);
        }

        if (DeclarationCore == "") {
          DeclarationCore = ParamTypeFull.getAsString();
        } else {
          DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
        }
      }
    }
  } else if (ParameterType->isFunctionPointerType()) {
    if (DeclarationCore == "") {
      DeclarationCore = "void*";
    } else {
      DeclarationCore = "void* " + DeclarationCore;
    }
  } else if (ParameterType->isPointerType()) {
    if (parameterType == RETVAL) {
      checkParameterType(ParameterType->getPointeeType(), isResolved,
                         dependencyList, DeclarationCore, parameterType);
      DeclarationCore += "*";
    } else {
      DeclarationCore = "(*" + DeclarationCore + ")";
      checkParameterType(ParameterType->getPointeeType(), isResolved,
                         dependencyList, DeclarationCore, parameterType);
    }
  } else if (ParameterType->isArrayType()) {
    std::string ArrayDeclaration =
        FFIBindingsUtils::getInstance()->getArraySize(
            Context->getAsConstantArrayType(ParameterType));
    DeclarationCore = DeclarationCore + ArrayDeclaration;
    checkParameterType(
        Context->getAsConstantArrayType(ParameterType)->getElementType(),
        isResolved, dependencyList, DeclarationCore, parameterType);
  } else if (ParameterType->isFundamentalType()) {
    if (DeclarationCore == "") {
      DeclarationCore = ParamTypeFull.getAsString();
    } else {
      DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
    }
  } else if (const VectorType *VT = ParameterType->getAs<VectorType>()) {
    if (VT->getVectorKind() == VectorType::GenericVector &&
        VT->getElementType()->isFundamentalType()) {
      DeclarationCore = VT->getElementType().getAsString() + " " +
                        DeclarationCore + " __attribute__((__vector_size__(" +
                        std::to_string(VT->getNumElements()) + " * sizeof(" +
                        VT->getElementType().getAsString() + "))))";
    }
  }
  // TODO: Add support for pointers to functions
}

bool FunctionVisitor::VisitFunctionDecl(FunctionDecl *FD) {

  if (!FFIBindingsUtils::getInstance()->isTestingModeOn()) {
    if (!FD->hasAttr<FFIBindingAttr>()) {
      return true;
    }
  }

  if (FFIBindingsUtils::getInstance()->isInResolvedDecls(
          "function " + FD->getQualifiedNameAsString()) ||
      FFIBindingsUtils::getInstance()->isInUnresolvedDeclarations(
          "function " + FD->getQualifiedNameAsString())) {
    return true;
  }

  bool isResolved = true;
  std::string FunctionDeclaration;
  std::vector<std::string> *dependencyList = new std::vector<std::string>();

  const FunctionType *FT = FD->getFunctionType();

  StorageClass storageClass = FD->getStorageClass();
  switch (storageClass) {
  case SC_Static:
    FunctionDeclaration = "static ";
    break;
  case SC_Extern:
    FunctionDeclaration = "extern ";
    break;
  default:
    break;
  }

  // check function return type
  QualType ReturnType = FT->getReturnType();

  std::string ReturnValueDeclaration;
  checkParameterType(ReturnType, &isResolved, dependencyList,
                     ReturnValueDeclaration, RETVAL);

  FunctionDeclaration += ReturnValueDeclaration + " ";
  FunctionDeclaration += FD->getQualifiedNameAsString() + "(";

  if (FD->getNumParams() == 0) {
    FunctionDeclaration += ");\n";
  } else {

    unsigned int i = 0;
    // check function parameters
    for (ParmVarDecl *PVD : FD->params()) {
      QualType ParameterType = PVD->getType();
      std::string Type = ParameterType.getAsString();
      std::string Name = PVD->getNameAsString();
      std::string ParameterDeclaration = Name;
      // if the parameter is (or a pointer to, or an array of) a record,
      // enumeration or typedef type, it should be resolved and printed before
      // this function's declaration

      checkParameterType(ParameterType, &isResolved, dependencyList,
                         ParameterDeclaration, PARAM);
      FunctionDeclaration += ParameterDeclaration;

      if (i < FD->getNumParams() - 1) {
        FunctionDeclaration += ", ";
      }
      i++;
      // TODO: If the parameter is e.g. a pointer to a pointer to a function,
      // that is not yet supported.
    }

    if (FD->isVariadic()) {
      FunctionDeclaration += ", ...";
    }
    FunctionDeclaration += ");\n";
  }

  if (isResolved) {
    FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
        "function " + FD->getQualifiedNameAsString());
    (*output) << FunctionDeclaration;
    (*output) << "\n";
    delete dependencyList;
  } else {
    DeclarationInfo FunctionDeclarationInfo;
    FunctionDeclarationInfo.isResolved = isResolved;
    FunctionDeclarationInfo.dependencyList = dependencyList;
    FunctionDeclarationInfo.Declaration = FunctionDeclaration;

    std::pair<std::string, DeclarationInfo> Function(
        "function " + FD->getQualifiedNameAsString(), FunctionDeclarationInfo);
    FFIBindingsUtils::getInstance()->getUnresolvedDeclarations()->insert(
        Function);
  }

  return true;
}
