#include "GenerateFFIBindings.hpp"

FFIBindingsUtils *FFIBindingsUtils::instance = NULL;

FFIBindingsUtils *FFIBindingsUtils::getInstance() {

  if (!instance)
    instance = new FFIBindingsUtils();

  return instance;
}

bool FFIBindingsUtils::isNewType(QualType Type) {

  if (isInResolvedDecls(Type.getAsString()))
    return false;

  if (isInUnresolvedDeclarations(Type.getAsString()))
    return false;

  return true;
}

bool FFIBindingsUtils::isInUnresolvedDeclarations(std::string DeclType) {

  if (UnresolvedDeclarations->find(DeclType) != UnresolvedDeclarations->end())
    return true;

  return false;
}

bool FFIBindingsUtils::isInResolvedDecls(std::string DeclType) {

  if (ResolvedDecls->find(DeclType) != ResolvedDecls->end())
    return true;

  return false;
}

bool FFIBindingsUtils::isOnBlacklist(std::string DeclType) {

  if (blacklist->find(DeclType) != blacklist->end())
    return true;

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
      if (appendSpace)
        ArraySize += ' ';
      ArraySize += "volatile";
      appendSpace = true;
    }
    if (TypeQuals & Qualifiers::Restrict) {
      if (appendSpace)
        ArraySize += ' ';
      ArraySize += "restrict";
    }
    ArraySize += ' ';
  }

  ArraySize += std::to_string(CAT->getSize().getZExtValue()) + ']';
  return ArraySize;
}

std::string FFIBindingsUtils::getDeclAttrs(Decl *D) {

  std::string attrList;
  if (!D->hasAttrs())
    return attrList;

  int numOfAttrs = 0;

  for (Attr *attr : D->getAttrs()) {
    if (attr->getKind() == attr::Kind::Packed) {
      if (numOfAttrs > 0)
        attrList = ", " + attrList;

      attrList = "packed" + attrList;
      numOfAttrs++;
    } else if (attr->getKind() == attr::Kind::Aligned) {
      if (numOfAttrs > 0)
        attrList = ", " + attrList;

      SourceLocation beginLoc = attr->getRange().getBegin();
      SourceLocation endLoc = attr->getRange().getEnd();
      const char *begin =
          D->getASTContext().getSourceManager().getCharacterData(beginLoc);
      const char *end =
          D->getASTContext().getSourceManager().getCharacterData(endLoc);
      attrList = std::string(begin, end + 1 - begin) + attrList;
      numOfAttrs++;
    }
  }

  if (numOfAttrs)
    attrList = "__attribute__((" + attrList + ")) ";
  return attrList;
}

void FFIBindingsUtils::resolveAnonRecord(RecordDecl *RD) {

  std::string AnonRecordName =
      RD->getTypeForDecl()->getCanonicalTypeInternal().getAsString();
  std::string RecordDeclaration;
  std::vector<std::string> *dependencyList = new std::vector<std::string>();
  bool isResolved = true;

  char separator;
#ifdef LLVM_ON_UNIX
  separator = '/';
#else
  separator = '\\';
#endif

  unsigned int j = 0;
  int distance;
  for (std::vector<std::string>::iterator i = getAnonymousRecords()->begin();
       i != getAnonymousRecords()->end(); ++i) {
    if (*i == AnonRecordName) {
      distance = std::distance(getAnonymousRecords()->begin(), i);
      break;
    }
    j++;
  }
  if (j == getAnonymousRecords()->size()) {
    getAnonymousRecords()->push_back(AnonRecordName);
    distance = getAnonymousRecords()->size() - 1;
  }

  int firstindex;
  if (AnonRecordName.find_last_of(separator) == std::string::npos)
    firstindex = AnonRecordName.find("at ") + 2;
  else
    firstindex = AnonRecordName.find_last_of(separator);

  AnonRecordName = AnonRecordName.substr(
      firstindex + 1, AnonRecordName.find_first_of(':') - firstindex - 1);
  std::replace(AnonRecordName.begin(), AnonRecordName.end(), '.', '_');
  std::replace(AnonRecordName.begin(), AnonRecordName.end(), '-', '_');
  AnonRecordName =
      "Anonymous_" + AnonRecordName + "_" + std::to_string(distance);

  std::string attrs = getDeclAttrs(RD);

  if (RD->getTypeForDecl()->isStructureType()) {
    RecordDeclaration = "struct " + attrs + AnonRecordName;
    AnonRecordName = "struct " + AnonRecordName;
  } else if (RD->getTypeForDecl()->isUnionType()) {
    RecordDeclaration = "union " + attrs + AnonRecordName;
    AnonRecordName = "union " + AnonRecordName;
  }

  RecordDeclaration += " {\n";
  // check the fields
  for (RecordDecl::field_iterator FI = RD->field_begin(); FI != RD->field_end();
       ++FI) {
    std::string FieldDeclaration = FI->getNameAsString();
    checkType(FI->getType(), &isResolved, dependencyList, FieldDeclaration,
              NORMAL, NONE);
    RecordDeclaration += FieldDeclaration + ";\n";
  }
  RecordDeclaration += "};\n";

  if (isResolved) {
    getResolvedDecls()->insert(AnonRecordName);
    (*output) += RecordDeclaration;
    (*output) += "\n";
    delete dependencyList;
  } else {
    // add this record to list of unresolved declarations
    DeclarationInfo RecordDeclarationInfo;
    RecordDeclarationInfo.isResolved = isResolved;
    RecordDeclarationInfo.dependencyList = dependencyList;
    RecordDeclarationInfo.Declaration = RecordDeclaration;

    std::pair<std::string, DeclarationInfo> Record(AnonRecordName,
                                                   RecordDeclarationInfo);
    getUnresolvedDeclarations()->insert(Record);
  }
}

void FFIBindingsUtils::resolveEnumDecl(EnumDecl *ED) {

  std::string EnumDeclaration;
  std::vector<std::string> elements;

  std::string attrs = getDeclAttrs(ED);
  EnumDeclaration = "enum " + attrs + ED->getNameAsString() + " {";

  int length = 0;
  for (EnumDecl::enumerator_iterator EI = ED->enumerator_begin();
       EI != ED->enumerator_end(); ++EI) {
    SmallString<32> EnumValue(EI->getNameAsString());
    if (EI->getInitExpr()) {
      EnumValue += " = ";
      EI->getInitVal().toString(EnumValue);
    }
    elements.push_back(EnumValue.str());
    length++;
  }

  for (int i = 0; i < length; i++) {
    if (i < length - 1)
      EnumDeclaration += elements[i] + ", ";
    else
      EnumDeclaration += elements[i] + "};\n";
  }

  (*output) += EnumDeclaration; // print the enumeration
  (*output) += "\n";
  getResolvedDecls()->insert("enum " + ED->getNameAsString());
}

void FFIBindingsUtils::resolveFunctionDecl(FunctionDecl *FD) {

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
  checkType(ReturnType, &isResolved, dependencyList, ReturnValueDeclaration,
            FUNCTION, RETVAL);

  FunctionDeclaration += ReturnValueDeclaration + " ";
  FunctionDeclaration += FD->getQualifiedNameAsString() + "(";

  if (FD->getNumParams() == 0)
    FunctionDeclaration += ");\n";
  else {
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
      checkType(ParameterType, &isResolved, dependencyList,
                ParameterDeclaration, FUNCTION, PARAM);
      FunctionDeclaration += ParameterDeclaration;

      if (i < FD->getNumParams() - 1)
        FunctionDeclaration += ", ";
      i++;
    }

    if (FD->isVariadic())
      FunctionDeclaration += ", ...";

    FunctionDeclaration += ");\n";
  }

  if (isResolved) {
    getResolvedDecls()->insert("function " + FD->getQualifiedNameAsString());
    (*output) += FunctionDeclaration;
    (*output) += "\n";
    delete dependencyList;
  } else {
    DeclarationInfo FunctionDeclarationInfo;
    FunctionDeclarationInfo.isResolved = isResolved;
    FunctionDeclarationInfo.dependencyList = dependencyList;
    FunctionDeclarationInfo.Declaration = FunctionDeclaration;

    std::pair<std::string, DeclarationInfo> Function(
        "function " + FD->getQualifiedNameAsString(), FunctionDeclarationInfo);
    getUnresolvedDeclarations()->insert(Function);
  }
}

void FFIBindingsUtils::resolveRecordDecl(RecordDecl *RD) {

  bool isResolved = true;
  std::string RecordDeclaration;
  std::vector<std::string> *dependencyList = new std::vector<std::string>();

  std::string attrList = getDeclAttrs(RD);

  if (RD->getTypeForDecl()->isStructureType())
    RecordDeclaration = "struct ";
  else if (RD->getTypeForDecl()->isUnionType())
    RecordDeclaration = "union ";

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
    checkType(FI->getType(), &isResolved, dependencyList, FieldDeclaration,
              NORMAL, NONE);
    RecordDeclaration += FieldDeclaration + ";\n";
  }
  RecordDeclaration += "};\n";
  if (isResolved) {
    getResolvedDecls()->insert(
        RD->getTypeForDecl()->getCanonicalTypeInternal().getAsString());
    if (RD->field_empty())
      (*output) +=
          RD->getTypeForDecl()->getCanonicalTypeInternal().getAsString() +
          ";\n";
    else
      (*output) += RecordDeclaration;
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
    getUnresolvedDeclarations()->insert(Record);
  }
}

void FFIBindingsUtils::resolveTypedefDecl(TypedefNameDecl *TD) {

  std::string TypedefDeclaration = "typedef ";

  QualType UnderlyingTypeFull = TD->getUnderlyingType();
  QualType UnderlyingType = TD->getUnderlyingType();
  unsigned Qualifiers = UnderlyingType.getLocalCVRQualifiers();
  UnderlyingType.removeLocalCVRQualifiers(Qualifiers);

  if (const TypedefType *TT = UnderlyingType->getAs<TypedefType>()) {

    TypedefNameDecl *typedefDecl = TT->getDecl();
    std::string TypedefDeclaration = "typedef " +
                                     UnderlyingTypeFull.getAsString() + " " +
                                     TD->getNameAsString() + ";\n";

    if (isOnBlacklist(UnderlyingType.getAsString()))
      getResolvedDecls()->insert("typedef " + TD->getNameAsString());
    else {
      bool isResolved = false;
      std::vector<std::string> *dependencyList = new std::vector<std::string>();

      TypeDeclaration TypedefTypeDeclaration;
      TypedefTypeDeclaration.Declaration = typedefDecl;
      TypedefTypeDeclaration.TypeName =
          "typedef " + UnderlyingType.getAsString();
      dependencyList->push_back("typedef " + UnderlyingType.getAsString());

      DeclarationInfo TypedefDeclarationInfo;
      TypedefDeclarationInfo.isResolved = isResolved;
      TypedefDeclarationInfo.dependencyList = dependencyList;
      TypedefDeclarationInfo.Declaration = TypedefDeclaration;

      std::pair<std::string, DeclarationInfo> TypedefDecl(
          "typedef " + TD->getNameAsString(), TypedefDeclarationInfo);

      getUnresolvedDeclarations()->insert(TypedefDecl);

      if (isNewType(UnderlyingType))
        getDeclsToFind()->push(TypedefTypeDeclaration);
    }

  } else if (UnderlyingType->isFundamentalType()) {

    TypedefDeclaration +=
        UnderlyingTypeFull.getAsString() + " " + TD->getNameAsString() + ";\n";
    getResolvedDecls()->insert("typedef " + TD->getNameAsString());
    (*output) += TypedefDeclaration;
    (*output) += "\n";

  } else if (UnderlyingType->isRecordType()) {

    bool isResolved = false;
    std::vector<std::string> *dependencyList = new std::vector<std::string>();

    const RecordType *RT = UnderlyingType->getAs<RecordType>();
    RecordDecl *recordDecl = RT->getDecl();

    if (isOnBlacklist(UnderlyingType.getAsString())) {
      getResolvedDecls()->insert(recordDecl->getTypeForDecl()
                                     ->getCanonicalTypeInternal()
                                     .getAsString());
      TypedefDeclaration += UnderlyingTypeFull.getAsString() + " ";
    } else {
      if (recordDecl->getNameAsString() == "") {
        std::string AnonRecordDeclaration;

        if (UnderlyingType->isStructureType())
          TypedefDeclaration += "struct ";
        else if (UnderlyingType->isUnionType())
          TypedefDeclaration += "union ";

        std::string attrList = getDeclAttrs(recordDecl);
        TypedefDeclaration += attrList + "{\n";

        for (RecordDecl::field_iterator FI = recordDecl->field_begin();
             FI != recordDecl->field_end(); ++FI) {
          std::string FieldDeclaration = FI->getNameAsString();
          checkType(FI->getType(), &isResolved, dependencyList,
                    FieldDeclaration, NORMAL, NONE);
          AnonRecordDeclaration += FieldDeclaration + ";\n";
        }

        TypedefDeclaration += AnonRecordDeclaration + "} ";

      } else {
        TypedefDeclaration += UnderlyingTypeFull.getAsString() + " ";
        dependencyList->push_back(recordDecl->getTypeForDecl()
                                      ->getCanonicalTypeInternal()
                                      .getAsString());

        if (isNewType(UnderlyingType)) {
          TypeDeclaration RecordTypeDeclaration;
          RecordTypeDeclaration.Declaration = recordDecl;
          RecordTypeDeclaration.TypeName = recordDecl->getTypeForDecl()
                                               ->getCanonicalTypeInternal()
                                               .getAsString();
          getDeclsToFind()->push(RecordTypeDeclaration);
        }
      }
    }

    TypedefDeclaration += TD->getNameAsString() + ";\n";

    DeclarationInfo TypedefDeclarationInfo;
    TypedefDeclarationInfo.isResolved = isResolved;
    TypedefDeclarationInfo.dependencyList = dependencyList;
    TypedefDeclarationInfo.Declaration = TypedefDeclaration;

    std::pair<std::string, DeclarationInfo> TypedefDecl(
        "typedef " + TD->getNameAsString(), TypedefDeclarationInfo);

    getUnresolvedDeclarations()->insert(TypedefDecl);

  } else if (UnderlyingType->isEnumeralType()) {

    const EnumType *ET = UnderlyingType->castAs<EnumType>();
    EnumDecl *enumDecl = ET->getDecl();
    std::string TypedefDeclaration = "typedef enum ";

    if (isOnBlacklist(UnderlyingType.getAsString())) {
      getResolvedDecls()->insert(UnderlyingType.getAsString());
      TypedefDeclaration += enumDecl->getNameAsString() + " ";
    } else {

      std::vector<std::string> elements;

      TypedefDeclaration += getDeclAttrs(enumDecl);

      if (enumDecl->getNameAsString() != "") {
        TypedefDeclaration += enumDecl->getNameAsString() + " ";
      }
      TypedefDeclaration += "{";

      int length = 0;
      for (EnumDecl::enumerator_iterator EI = enumDecl->enumerator_begin();
           EI != enumDecl->enumerator_end(); ++EI) {
        SmallString<32> EnumValue(EI->getNameAsString());
        if (EI->getInitExpr()) {
          EnumValue += " = ";
          EI->getInitVal().toString(EnumValue);
        }
        elements.push_back(EnumValue.str());
        length++;
      }

      for (int i = 0; i < length; i++) {
        if (i < length - 1) {
          TypedefDeclaration += elements[i] + ", ";
        } else {
          TypedefDeclaration += elements[i] + "} ";
        }
      }
    }
    TypedefDeclaration += TD->getNameAsString() + ";\n";
    (*output) += TypedefDeclaration; // print the typedef
    (*output) += "\n";

    getResolvedDecls()->insert("typedef " + TD->getNameAsString());

  } else if (UnderlyingType->isFunctionPointerType()) {

    std::string FunctionPointerDeclarationCore;
    bool isResolved = false;
    std::vector<std::string> *dependencyList = new std::vector<std::string>();
    const FunctionProtoType *FPT =
        (const FunctionProtoType *)
        UnderlyingType->getPointeeType()->getAs<FunctionType>();
    std::string ReturnValueDeclaration;

    checkType(FPT->getReturnType(), &isResolved, dependencyList,
              ReturnValueDeclaration, FUNCTION, RETVAL);
    std::string TypedefDeclaration = "typedef " + ReturnValueDeclaration +
                                     " (*" + TD->getNameAsString() + ")" + "(";

    unsigned int NumOfParams = FPT->getNumParams();
    for (unsigned int i = 0; i < NumOfParams; i++) {
      std::string ParameterDeclaration;
      checkType(FPT->getParamType(i), &isResolved, dependencyList,
                ParameterDeclaration, FUNCTION, PARAM);
      FunctionPointerDeclarationCore += ParameterDeclaration;
      if (i != NumOfParams - 1)
        FunctionPointerDeclarationCore += ", ";
    }

    if (FPT->isVariadic())
      FunctionPointerDeclarationCore += ", ...";

    FunctionPointerDeclarationCore += ");\n";
    TypedefDeclaration += FunctionPointerDeclarationCore;

    DeclarationInfo TypedefDeclarationInfo;
    TypedefDeclarationInfo.isResolved = isResolved;
    TypedefDeclarationInfo.dependencyList = dependencyList;
    TypedefDeclarationInfo.Declaration = TypedefDeclaration;

    std::pair<std::string, DeclarationInfo> TypedefDecl(
        "typedef " + TD->getNameAsString(), TypedefDeclarationInfo);

    getUnresolvedDeclarations()->insert(TypedefDecl);

  } else if (UnderlyingType->isPointerType()) {

    bool isResolved = false;
    std::vector<std::string> *dependencyList = new std::vector<std::string>();
    std::string TypedefDeclaration = "typedef ";
    std::string ElementType;

    if (UnderlyingType->getPointeeType()->isFundamentalType() &&
        !(UnderlyingType->getPointeeType()->getAs<TypedefType>())) {
      TypedefDeclaration += UnderlyingTypeFull.getAsString() + " " +
                            TD->getNameAsString() + ";\n";
      isResolved = true;
    } else {
      std::string DeclarationCore = "(*" + TD->getNameAsString() + ")";
      checkType(UnderlyingType->getPointeeType(), &isResolved, dependencyList,
                DeclarationCore, NORMAL, NONE);

      TypedefDeclaration += DeclarationCore + ";\n";
      isResolved = false;

      DeclarationInfo TypedefDeclarationInfo;
      TypedefDeclarationInfo.isResolved = isResolved;
      TypedefDeclarationInfo.dependencyList = dependencyList;
      TypedefDeclarationInfo.Declaration = TypedefDeclaration;

      std::pair<std::string, DeclarationInfo> TypedefDecl(
          "typedef " + TD->getNameAsString(), TypedefDeclarationInfo);

      getUnresolvedDeclarations()->insert(TypedefDecl);
    }
    if (isResolved) {
      getResolvedDecls()->insert("typedef " + TD->getNameAsString());
      (*output) += TypedefDeclaration; // print the typedef
      (*output) += "\n";

      delete dependencyList;
    }
  } else if (UnderlyingType->isArrayType()) {

    std::string TypedefDeclaration = "typedef ";

    bool isResolved = false;
    std::vector<std::string> *dependencyList = new std::vector<std::string>();
    std::string DeclarationCore = TD->getNameAsString();

    std::string ArrayDeclaration;
    QualType ElementType;

    if (UnderlyingType->isConstantArrayType()) {
      ArrayDeclaration =
          getArraySize(Context->getAsConstantArrayType(UnderlyingType));
      ElementType =
          Context->getAsConstantArrayType(UnderlyingType)->getElementType();
    } else if (UnderlyingType->isIncompleteArrayType()) {
      ArrayDeclaration = "[]";
      ElementType =
          Context->getAsIncompleteArrayType(UnderlyingType)->getElementType();
    }

    DeclarationCore += ArrayDeclaration;
    checkType(ElementType, &isResolved, dependencyList, DeclarationCore, NORMAL,
              NONE);

    TypedefDeclaration += DeclarationCore + ";\n";

    DeclarationInfo TypedefDeclarationInfo;
    TypedefDeclarationInfo.isResolved = isResolved;
    TypedefDeclarationInfo.dependencyList = dependencyList;
    TypedefDeclarationInfo.Declaration = TypedefDeclaration;

    std::pair<std::string, DeclarationInfo> TypedefDecl(
        "typedef " + TD->getNameAsString(), TypedefDeclarationInfo);

    getUnresolvedDeclarations()->insert(TypedefDecl);

  } else if (const VectorType *VT = UnderlyingType->getAs<VectorType>()) {

    if (VT->getVectorKind() == VectorType::GenericVector &&
        VT->getElementType()->isFundamentalType()) {
      TypedefDeclaration +=
          VT->getElementType().getAsString() + " " + TD->getNameAsString() +
          " __attribute__((__vector_size__(" +
          std::to_string(VT->getNumElements()) + " * sizeof(" +
          VT->getElementType().getAsString() + "))));\n";
      getResolvedDecls()->insert("typedef " + TD->getNameAsString());
      (*output) += TypedefDeclaration;
      (*output) += "\n";
    }
  }
}

void FFIBindingsUtils::checkType(QualType ParameterType, bool *isResolved,
                                 std::vector<std::string> *dependencyList,
                                 std::string &DeclarationCore,
                                 enum ParentDeclType type,
                                 enum ParamType parameterType) {

  unsigned Qualifiers = ParameterType.getLocalCVRQualifiers();
  QualType ParamTypeFull = ParameterType;
  ParameterType.removeLocalCVRQualifiers(Qualifiers);

  // typedef type should be checked for first, because e.g. a typedef of a
  // structure is both typedef type and record type
  if (const TypedefType *TT = ParameterType->getAs<TypedefType>()) {

    if (isOnBlacklist(ParameterType.getAsString()))
      getResolvedDecls()->insert("typedef " + ParameterType.getAsString());
    else {
      TypeDeclaration TypedefTypeDeclaration;
      TypedefTypeDeclaration.Declaration = TT->getDecl();
      TypedefTypeDeclaration.TypeName =
          "typedef " + ParameterType.getAsString();

      *isResolved = false;
      dependencyList->push_back("typedef " + ParameterType.getAsString());

      if (isNewType(ParameterType))
        getDeclsToFind()->push(TypedefTypeDeclaration);
    }
    if (DeclarationCore == "")
      DeclarationCore = ParamTypeFull.getAsString();
    else
      DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;

  } else if (const RecordType *RT = ParameterType->getAs<RecordType>()) {

    if (isOnBlacklist(ParameterType.getAsString())) {
      getResolvedDecls()->insert(ParameterType.getAsString());
      if (DeclarationCore == "")
        DeclarationCore = ParamTypeFull.getAsString();
      else
        DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
    } else {

      RecordDecl *RD = RT->getDecl();
      TypeDeclaration RecordTypeDeclaration;
      RecordTypeDeclaration.Declaration = RD;

      if (RD->getNameAsString() == "") {

        if (type == FUNCTION) {
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
                   getAnonymousRecords()->begin();
               i != getAnonymousRecords()->end(); ++i) {
            if (*i == AnonRecordName) {
              distance = std::distance(getAnonymousRecords()->begin(), i);
              break;
            }
            j++;
          }
          if (j == getAnonymousRecords()->size()) {
            getAnonymousRecords()->push_back(AnonRecordName);
            distance = getAnonymousRecords()->size() - 1;
          }

          int firstindex;
          if (AnonRecordName.find_last_of(separator) == std::string::npos) {
            firstindex = AnonRecordName.find("at ") + 2;
          } else {
            firstindex = AnonRecordName.find_last_of(separator);
          }

          AnonRecordName = AnonRecordName.substr(
              firstindex + 1,
              AnonRecordName.find_first_of(':') - firstindex - 1);
          std::replace(AnonRecordName.begin(), AnonRecordName.end(), '.', '_');
          std::replace(AnonRecordName.begin(), AnonRecordName.end(), '-', '_');
          AnonRecordName =
              "Anonymous_" + AnonRecordName + "_" + std::to_string(distance);

          if (RT->isStructureType())
            AnonRecordName = "struct " + AnonRecordName;
          else if (RT->isUnionType())
            AnonRecordName = "union " + AnonRecordName;

          *isResolved = false;

          RecordTypeDeclaration.TypeName = AnonRecordName;
          dependencyList->push_back(AnonRecordName);

          if (!isInResolvedDecls(AnonRecordName) &&
              !isInUnresolvedDeclarations(AnonRecordName))
            getDeclsToFind()->push(RecordTypeDeclaration);

          if (DeclarationCore == "")
            DeclarationCore = AnonRecordName;
          else
            DeclarationCore = AnonRecordName + " " + DeclarationCore;

        } else if (type == NORMAL) {
          std::string AnonRecordDeclaration;

          if (RT->isStructureType())
            AnonRecordDeclaration += "struct ";
          else if (RT->isUnionType())
            AnonRecordDeclaration += "union ";

          AnonRecordDeclaration += getDeclAttrs(RD);
          AnonRecordDeclaration += "{\n";

          for (RecordDecl::field_iterator FI = RD->field_begin();
               FI != RD->field_end(); ++FI) {
            std::string FieldDeclaration = FI->getNameAsString();
            checkType(FI->getType(), isResolved, dependencyList,
                      FieldDeclaration, NORMAL, NONE);
            AnonRecordDeclaration += FieldDeclaration + ";\n";
          }

          AnonRecordDeclaration += "}";

          if (!RD->isAnonymousStructOrUnion())
            AnonRecordDeclaration += " ";

          DeclarationCore = AnonRecordDeclaration + DeclarationCore;
        }

      } else {
        RecordTypeDeclaration.TypeName = ParameterType.getAsString();

        *isResolved = false;
        dependencyList->push_back(ParameterType.getAsString());

        if (isNewType(ParameterType))
          getDeclsToFind()->push(RecordTypeDeclaration);

        if (DeclarationCore == "")
          DeclarationCore = ParamTypeFull.getAsString();
        else
          DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
      }
    }

  } else if (const EnumType *ET = ParameterType->getAs<EnumType>()) {

    if (isOnBlacklist(ParameterType.getAsString())) {
      getResolvedDecls()->insert(ParameterType.getAsString());
      if (DeclarationCore == "")
        DeclarationCore = ParamTypeFull.getAsString();
      else
        DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
    } else {
      EnumDecl *ED = ET->getDecl();

      if (ED->getNameAsString() == "") {
        std::vector<std::string> elements;
        std::string AnonEnumDeclaration;

        std::string attrs = getDeclAttrs(ED);
        AnonEnumDeclaration += "enum " + attrs + "{";

        int length = 0;
        for (EnumDecl::enumerator_iterator EI = ED->enumerator_begin();
             EI != ED->enumerator_end(); ++EI) {
          SmallString<32> EnumValue(EI->getNameAsString());
          if (EI->getInitExpr()) {
            EnumValue += " = ";
            EI->getInitVal().toString(EnumValue);
          }
          elements.push_back(EnumValue.str());
          length++;
        }

        for (int i = 0; i < length; i++) {
          if (i < length - 1)
            AnonEnumDeclaration += elements[i] + ", ";
          else {
            if (type == FUNCTION)
              AnonEnumDeclaration += elements[i] + "}";
            else if (type == NORMAL)
              AnonEnumDeclaration += elements[i] + "} ";
          }
        }
        DeclarationCore = AnonEnumDeclaration + DeclarationCore;
      } else {
        TypeDeclaration EnumTypeDeclaration;
        EnumTypeDeclaration.Declaration = ED;
        EnumTypeDeclaration.TypeName = ParameterType.getAsString();

        *isResolved = false;
        dependencyList->push_back(ParameterType.getAsString());

        if (isNewType(ParameterType))
          getDeclsToFind()->push(EnumTypeDeclaration);

        if (DeclarationCore == "")
          DeclarationCore = ParamTypeFull.getAsString();
        else
          DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
      }
    }

  } else if (ParameterType->isFunctionPointerType()) {

    std::string FunctionPointerDeclarationCore;
    const FunctionProtoType *FPT =
        (const FunctionProtoType *)
        ParameterType->getPointeeType()->getAs<FunctionType>();
    std::string ReturnValueDeclaration;

    checkType(FPT->getReturnType(), isResolved, dependencyList,
              ReturnValueDeclaration, FUNCTION, RETVAL);
    DeclarationCore =
        ReturnValueDeclaration + " (*" + DeclarationCore + ")" + "(";

    unsigned int NumOfParams = FPT->getNumParams();
    for (unsigned int i = 0; i < NumOfParams; i++) {
      std::string ParameterDeclaration;
      checkType(FPT->getParamType(i), isResolved, dependencyList,
                ParameterDeclaration, FUNCTION, PARAM);
      FunctionPointerDeclarationCore += ParameterDeclaration;
      if (i != NumOfParams - 1)
        FunctionPointerDeclarationCore += ", ";
    }

    if (FPT->isVariadic())
      FunctionPointerDeclarationCore += ", ...";

    FunctionPointerDeclarationCore += ")";
    DeclarationCore += FunctionPointerDeclarationCore;

  } else if (ParameterType->isPointerType()) {

    if (type == FUNCTION) {
      if (parameterType == RETVAL) {
        checkType(ParameterType->getPointeeType(), isResolved, dependencyList,
                  DeclarationCore, type, parameterType);
        DeclarationCore += "*";
      } else {
        DeclarationCore = "(*" + DeclarationCore + ")";
        checkType(ParameterType->getPointeeType(), isResolved, dependencyList,
                  DeclarationCore, type, parameterType);
      }
    } else if (type == NORMAL) {
      DeclarationCore = "(*" + DeclarationCore + ")";
      checkType(ParameterType->getPointeeType(), isResolved, dependencyList,
                DeclarationCore, type, parameterType);
    }
  } else if (ParameterType->isArrayType()) {
    std::string ArrayDeclaration;
    QualType ElementType;

    if (ParameterType->isConstantArrayType()) {
      ArrayDeclaration =
          getArraySize(Context->getAsConstantArrayType(ParameterType));
      ElementType =
          Context->getAsConstantArrayType(ParameterType)->getElementType();
    } else if (ParameterType->isIncompleteArrayType()) {
      ArrayDeclaration = "[]";
      ElementType =
          Context->getAsIncompleteArrayType(ParameterType)->getElementType();
    }

    DeclarationCore += ArrayDeclaration;
    checkType(ElementType, isResolved, dependencyList, DeclarationCore, type,
              parameterType);
  } else if (ParameterType->isFundamentalType()) {

    if (DeclarationCore == "")
      DeclarationCore = ParamTypeFull.getAsString();
    else
      DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;

  } else if (const VectorType *VT = ParameterType->getAs<VectorType>()) {

    if (VT->getVectorKind() == VectorType::GenericVector &&
        VT->getElementType()->isFundamentalType())
      DeclarationCore = VT->getElementType().getAsString() + " " +
                        DeclarationCore + " __attribute__((__vector_size__(" +
                        std::to_string(VT->getNumElements()) + " * sizeof(" +
                        VT->getElementType().getAsString() + "))))";
  }
}
