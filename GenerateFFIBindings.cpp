//===- GenerateFFIBindings.cpp
//---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Clang plugin which generates LuaJIT ffi bindings for functions,
// records (structs and unions) and enums in the input file that are marked with
// the ffi binding attribute.
//
//===----------------------------------------------------------------------===//

#include "GenerateFFIBindings.hpp"
/**
 * Class used for generating required FFI bindings by traversing through
 * the parsed AST and extracting relevant information from the
 * appropriate nodes.
 *
 * Implementation of the ASTConsumer interface.
 * It implements HandleTranslationUnit method, which gets called when
 * the AST for entire translation unit has been parsed.
 *
 **/
class GenerateFFIBindingsConsumer : public ASTConsumer {
public:
  GenerateFFIBindingsConsumer() {}
  virtual void HandleTranslationUnit(clang::ASTContext &context) {

    DiagnosticsEngine &DE = context.getDiagnostics();
    if (DE.hasErrorOccurred()) {
      llvm::outs() << "----------------------------------\n";
      llvm::outs() << "--------- ffi-gen plugin ---------\n";
      llvm::outs() << "----------------------------------\n";
      llvm::outs() << "Error has occurred during compilation ";
      llvm::outs() << "- ffi bindings will not be generated.\n\n";
      return;
    }

    std::error_code Err;
    utils = FFIBindingsUtils::getInstance();

    std::string outputFileName = utils->getOutputFileName();
    std::string headerFileName = utils->getHeaderFileName();
    std::string blacklistFileName = utils->getBlacklistFileName();

    std::string sourceFileName =
        context.getSourceManager()
            .getFileEntryForID(context.getSourceManager().getMainFileID())
            ->getName();
    std::string dirName =
        context.getSourceManager().getFileManager().getCanonicalName(
            context.getSourceManager()
                .getFileEntryForID(context.getSourceManager().getMainFileID())
                ->getDir());

    char separator;
#ifdef LLVM_ON_UNIX
    separator = '/';
#else
    separator = '\\';
#endif
    dirName += separator;
    if (sourceFileName.find_last_of(separator) != std::string::npos) {
      sourceFileName =
          sourceFileName.substr(sourceFileName.find_last_of(separator) + 1);
    }
    if (utils->isTestingModeOn()) {
      outputFileName = sourceFileName;
      outputFileName.replace(
          outputFileName.find_first_of('.'),
          outputFileName.length() - outputFileName.find_first_of('.'), "");
      outputFileName = dirName + outputFileName;
      std::replace(outputFileName.begin(), outputFileName.end(), separator,
                   '_');

      outputFileName += ".lua";
    }

    sourceFileName = ">> " + dirName + sourceFileName;

    if (headerFileName != "") {
      std::string line;
      std::ifstream headerFile(headerFileName);
      if (headerFile.is_open()) {
        while (getline(headerFile, line)) {
          if (line.find(constants::SRC_FILE_PLACE_HOLDER) !=
              std::string::npos) {
            line.replace(line.find(constants::SRC_FILE_PLACE_HOLDER),
                         constants::SRC_FILE_PLACE_HOLDER.length(),
                         sourceFileName);
          }
          output += line + "\n";
        }
        headerFile.close();
      } else {
        llvm::outs() << "Error opening file: \"" << headerFileName << "\"\n";
        return;
      }
    }

    if (blacklistFileName != "") {
      std::string line;
      std::ifstream blacklistFile(blacklistFileName);
      if (blacklistFile.is_open()) {
        while (getline(blacklistFile, line)) {
          utils->getBlacklist()->insert(line);
        }
        blacklistFile.close();
      } else {
        llvm::outs() << "Error opening file: \"" << blacklistFileName << "\"\n";
        return;
      }
    }

    output += "ffi = require(\"ffi\")\nffi.cdef[[\n\n";

    FunctionsVisitor.setOutput(&output);
    RecordsVisitor.setOutput(&output);
    EnumsVisitor.setOutput(&output);

    // visit all function declarations and extract information
    // about unresolved dependencies, if there are any
    FunctionsVisitor.TraverseDecl(context.getTranslationUnitDecl());
    // visit all record declarations that are marked with ffibinding attribute
    // and start resolving them
    RecordsVisitor.setContext(&context);
    RecordsVisitor.TraverseDecl(context.getTranslationUnitDecl());
    // visit all enum declarations that are marked with ffibinding attribute
    // and print them out
    EnumsVisitor.setContext(&context);
    EnumsVisitor.TraverseDecl(context.getTranslationUnitDecl());

    // go through DeclsToFind until all required declarations are found
    while (utils->getDeclsToFind()->size() > 0) {

      TypeDeclaration Decl = utils->getDeclsToFind()->top();
      utils->getDeclsToFind()->pop();

      const Type *DeclType = Decl.Declaration->getTypeForDecl();
      if (!utils->isInResolvedDecls(Decl.TypeName) &&
          !utils->isInUnresolvedDeclarations(Decl.TypeName)) {
        if (DeclType->getAs<TypedefType>()) {
          resolveTypedefType(Decl);
        } else if (DeclType->isRecordType()) {
          if (Decl.Declaration->getNameAsString() == "") {
            resolveAnonRecord((RecordDecl *)Decl.Declaration);
          } else {
            RecordsVisitor.findRecordDeclaration(
                (RecordDecl *)Decl.Declaration);
          }
        } else if (DeclType->isEnumeralType()) {
          resolveEnumType(Decl);
        }
      }
    }
    unsigned int i = 0;
    bool possibleLoop = false;
    while (i < utils->getUnresolvedDeclarations()->size()) {
      i = 0;
      bool printedSomething = false;
      // iterate through the list of unresolved declarations until
      // they are all printed (become resolved);
      // declaration printing must be done in a certain order
      for (std::map<std::string, DeclarationInfo>::iterator it =
               utils->getUnresolvedDeclarations()->begin();
           it != utils->getUnresolvedDeclarations()->end(); ++it) {
        std::pair<std::string, DeclarationInfo> Decl = *it;
        std::string DeclName = Decl.first;
        DeclarationInfo DeclInfo = Decl.second;
        if (DeclInfo.isResolved) {
          i++;      // increment the number of already resolved declarations
          continue; // continue to the next one in the list
        }
        unsigned int j =
            0; // a counter used for checking if all dependencies are resolved

        for (std::vector<std::string>::iterator it1 =
                 DeclInfo.dependencyList->begin();
             it1 != DeclInfo.dependencyList->end(); ++it1) {
          // check if this dependency is resolved
          if (utils->isInResolvedDecls(*it1)) {
            j++;
          } else {
            break;
          }
        }
        if (j == DeclInfo.dependencyList->size()) { // if all dependencies of
                                                    // this declaration are
                                                    // resolved, it can be
                                                    // printed out and marked as
                                                    // resolved
          (*it).second.isResolved = true;
          utils->getResolvedDecls()->insert(DeclName);
          output += DeclInfo.Declaration; // print out the declaration
          output += "\n";
          printedSomething = true;
          possibleLoop = false;
          delete DeclInfo.dependencyList;
        } else {
          if (possibleLoop && !utils->isInResolvedDecls(DeclName) &&
              DependsOnItself(DeclName, DeclInfo.dependencyList)) {
            utils->getResolvedDecls()->insert(DeclName);
            output += DeclName; // print out the forward declaration
            output += ";\n";
            printedSomething = true;
            possibleLoop = false;
          }
          continue; // if this declaration is not yet ready for printing, move
                    // on to the next one in the list
        }
      }
      if (!printedSomething) {
        possibleLoop = true;
      }
    }
    output += "]]\n";

    if (utils->hasMarkedDeclarations()) {
      llvm::raw_fd_ostream *fileOutput = new llvm::raw_fd_ostream(
          utils->getDestinationDirectory() + outputFileName, Err,
          llvm::sys::fs::F_RW);
      if (Err) {
        llvm::errs() << "Error creating file \"" << outputFileName
                     << "\" : " << Err.message() << "!\n";
        return;
      }
      (*fileOutput) << output;
      (*fileOutput).close();
      delete fileOutput;
    }
    delete utils;
  }

private:
  FunctionVisitor FunctionsVisitor;
  RecordVisitor RecordsVisitor;
  EnumVisitor EnumsVisitor;
  ASTContext *Context;
  std::string output;
  FFIBindingsUtils *utils;

  void resolveEnumType(TypeDeclaration EnumTypeDeclaration) {

    std::string EnumDeclaration;
    std::vector<std::string> elements;
    EnumDecl *ED = (EnumDecl *)EnumTypeDeclaration.Declaration;

    std::string attrs = FFIBindingsUtils::getInstance()->getDeclAttrs(ED);
    EnumDeclaration = "enum " + attrs + ED->getNameAsString() + " {";

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
        EnumDeclaration += elements[i] + "};\n";
      }
    }

    output += EnumDeclaration; // print the enumeration
    output += "\n";
    utils->getResolvedDecls()->insert("enum " + ED->getNameAsString());
  }

  void resolveTypedefType(TypeDeclaration TypedefTypeDeclaration) {

    TypedefNameDecl *TD = (TypedefNameDecl *)TypedefTypeDeclaration.Declaration;

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

      if (utils->isOnBlacklist(UnderlyingType.getAsString())) {
        utils->getResolvedDecls()->insert("typedef " + TD->getNameAsString());
      } else {
        bool isResolved = false;
        std::vector<std::string> *dependencyList =
            new std::vector<std::string>();

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

        utils->getUnresolvedDeclarations()->insert(TypedefDecl);

        if (utils->isNewType(UnderlyingType)) {
          utils->getDeclsToFind()->push(TypedefTypeDeclaration);
        }
      }
    } else if (UnderlyingType->isFundamentalType()) {
      TypedefDeclaration += UnderlyingTypeFull.getAsString() + " " +
                            TD->getNameAsString() + ";\n";
      utils->getResolvedDecls()->insert("typedef " + TD->getNameAsString());
      output += TypedefDeclaration;
      output += "\n";
    } else if (UnderlyingType->isRecordType()) {

      bool isResolved = false;
      std::vector<std::string> *dependencyList = new std::vector<std::string>();

      const RecordType *RT = UnderlyingType->getAs<RecordType>();
      RecordDecl *recordDecl = RT->getDecl();

      if (utils->isOnBlacklist(UnderlyingType.getAsString())) {
        utils->getResolvedDecls()->insert(recordDecl->getTypeForDecl()
                                              ->getCanonicalTypeInternal()
                                              .getAsString());
        TypedefDeclaration += UnderlyingTypeFull.getAsString() + " ";
      } else {
        if (recordDecl->getNameAsString() == "") {
          std::string AnonRecordDeclaration;

          if (UnderlyingType->isStructureType()) {
            TypedefDeclaration += "struct ";
          } else if (UnderlyingType->isUnionType()) {
            TypedefDeclaration += "union ";
          }
          std::string attrList =
              FFIBindingsUtils::getInstance()->getDeclAttrs(recordDecl);
          TypedefDeclaration += attrList;
          TypedefDeclaration += "{\n";

          for (RecordDecl::field_iterator FI = recordDecl->field_begin();
               FI != recordDecl->field_end(); ++FI) {
            std::string FieldDeclaration = FI->getNameAsString();
            RecordsVisitor.checkFieldType(FI->getType(), &isResolved,
                                          dependencyList, FieldDeclaration);
            AnonRecordDeclaration += FieldDeclaration + ";\n";
          }

          TypedefDeclaration += AnonRecordDeclaration;

          TypedefDeclaration += "} ";

        } else {
          TypedefDeclaration += UnderlyingTypeFull.getAsString() + " ";
          dependencyList->push_back(recordDecl->getTypeForDecl()
                                        ->getCanonicalTypeInternal()
                                        .getAsString());

          if (utils->isNewType(UnderlyingType)) {
            TypeDeclaration RecordTypeDeclaration;
            RecordTypeDeclaration.Declaration = recordDecl;
            RecordTypeDeclaration.TypeName = recordDecl->getTypeForDecl()
                                                 ->getCanonicalTypeInternal()
                                                 .getAsString();
            utils->getDeclsToFind()->push(RecordTypeDeclaration);
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

      utils->getUnresolvedDeclarations()->insert(TypedefDecl);

    } else if (UnderlyingType->isEnumeralType()) {

      const EnumType *ET = UnderlyingType->castAs<EnumType>();
      EnumDecl *enumDecl = ET->getDecl();
      std::string TypedefDeclaration = "typedef enum ";

      if (utils->isOnBlacklist(UnderlyingType.getAsString())) {
        utils->getResolvedDecls()->insert(UnderlyingType.getAsString());
        TypedefDeclaration += enumDecl->getNameAsString() + " ";
      } else {

        std::vector<std::string> elements;

        TypedefDeclaration +=
            FFIBindingsUtils::getInstance()->getDeclAttrs(enumDecl);

        if (enumDecl->getNameAsString() != "") {
          TypedefDeclaration += enumDecl->getNameAsString() + " ";
        }
        TypedefDeclaration += "{";

        int length = 0;
        for (EnumDecl::enumerator_iterator EI = enumDecl->enumerator_begin();
             EI != enumDecl->enumerator_end(); ++EI) {
          elements.push_back(EI->getNameAsString());
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
      output += TypedefDeclaration; // print the typedef
      output += "\n";

      utils->getResolvedDecls()->insert("typedef " + TD->getNameAsString());

    } else if (UnderlyingType->isFunctionPointerType()) {
      std::string FunctionPointerDeclarationCore;
      bool isResolved = false;
      std::vector<std::string> *dependencyList = new std::vector<std::string>();

      const FunctionProtoType *FPT =
          (const FunctionProtoType *)
          UnderlyingType->getPointeeType()->getAs<FunctionType>();
      std::string ReturnValueDeclaration;
      FunctionsVisitor.checkParameterType(
          FPT->getReturnType(), &isResolved, dependencyList,
          ReturnValueDeclaration, FunctionsVisitor.RETVAL);
      std::string TypedefDeclaration = "typedef " + ReturnValueDeclaration +
                                       " (*" + TD->getNameAsString() + ")" +
                                       "(";
      unsigned int NumOfParams = FPT->getNumParams();
      for (unsigned int i = 0; i < NumOfParams; i++) {
        std::string ParameterDeclaration;
        FunctionsVisitor.checkParameterType(
            FPT->getParamType(i), &isResolved, dependencyList,
            ParameterDeclaration, FunctionsVisitor.PARAM);
        FunctionPointerDeclarationCore += ParameterDeclaration;
        if (i != NumOfParams - 1) {
          FunctionPointerDeclarationCore += ", ";
        }
      }
      if (FPT->isVariadic()) {
        FunctionPointerDeclarationCore += ", ...";
      }
      FunctionPointerDeclarationCore += ");\n";
      TypedefDeclaration += FunctionPointerDeclarationCore;

      DeclarationInfo TypedefDeclarationInfo;
      TypedefDeclarationInfo.isResolved = isResolved;
      TypedefDeclarationInfo.dependencyList = dependencyList;
      TypedefDeclarationInfo.Declaration = TypedefDeclaration;

      std::pair<std::string, DeclarationInfo> TypedefDecl(
          "typedef " + TD->getNameAsString(), TypedefDeclarationInfo);

      utils->getUnresolvedDeclarations()->insert(TypedefDecl);

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
        checkPointerType(UnderlyingType->getPointeeType(), &isResolved,
                         dependencyList, DeclarationCore);

        TypedefDeclaration += DeclarationCore + ";\n";
        isResolved = false;

        DeclarationInfo TypedefDeclarationInfo;
        TypedefDeclarationInfo.isResolved = isResolved;
        TypedefDeclarationInfo.dependencyList = dependencyList;
        TypedefDeclarationInfo.Declaration = TypedefDeclaration;

        std::pair<std::string, DeclarationInfo> TypedefDecl(
            "typedef " + TD->getNameAsString(), TypedefDeclarationInfo);

        utils->getUnresolvedDeclarations()->insert(TypedefDecl);
      }
      if (isResolved) {
        utils->getResolvedDecls()->insert("typedef " + TD->getNameAsString());
        output += TypedefDeclaration; // print the typedef
        output += "\n";

        delete dependencyList;
      }
    } else if (UnderlyingType->isArrayType()) {
      std::string TypedefDeclaration = "typedef ";
      std::string ArrayDeclaration;
      bool isResolved = false;
      std::vector<std::string> *dependencyList = new std::vector<std::string>();
      std::string DeclarationCore = TD->getNameAsString();

      if (UnderlyingType->isConstantArrayType()) {
        const ConstantArrayType *CAT =
            Context->getAsConstantArrayType(UnderlyingType);
        ArrayDeclaration = utils->getArraySize(CAT);
        DeclarationCore += ArrayDeclaration;
        checkPointerType(CAT->getElementType(), &isResolved, dependencyList,
                         DeclarationCore);
        TypedefDeclaration += DeclarationCore + ";\n";

        DeclarationInfo TypedefDeclarationInfo;
        TypedefDeclarationInfo.isResolved = isResolved;
        TypedefDeclarationInfo.dependencyList = dependencyList;
        TypedefDeclarationInfo.Declaration = TypedefDeclaration;

        std::pair<std::string, DeclarationInfo> TypedefDecl(
            "typedef " + TD->getNameAsString(), TypedefDeclarationInfo);

        utils->getUnresolvedDeclarations()->insert(TypedefDecl);
      }
    } else if (const VectorType *VT = UnderlyingType->getAs<VectorType>()) {
      if (VT->getVectorKind() == VectorType::GenericVector &&
          VT->getElementType()->isFundamentalType()) {
        TypedefDeclaration +=
            VT->getElementType().getAsString() + " " + TD->getNameAsString() +
            " __attribute__((__vector_size__(" +
            std::to_string(VT->getNumElements()) + " * sizeof(" +
            VT->getElementType().getAsString() + "))));\n";
        utils->getResolvedDecls()->insert("typedef " + TD->getNameAsString());
        output += TypedefDeclaration;
        output += "\n";
      }
    }
  }

  void checkPointerType(QualType ParameterType, bool *isResolved,
                        std::vector<std::string> *dependencyList,
                        std::string &DeclarationCore) {

    unsigned Qualifiers = ParameterType.getLocalCVRQualifiers();
    QualType ParamTypeFull = ParameterType;
    ParameterType.removeLocalCVRQualifiers(Qualifiers);

    // typedef type should be checked for first, because e.g. a typedef of a
    // structure is both typedef type and record type
    if (const TypedefType *TT = ParameterType->getAs<TypedefType>()) {
      if (utils->isOnBlacklist(ParameterType.getAsString())) {
        utils->getResolvedDecls()->insert("typedef " +
                                          ParameterType.getAsString());
      } else {
        TypedefNameDecl *TD = TT->getDecl();

        TypeDeclaration TypedefTypeDeclaration;
        TypedefTypeDeclaration.Declaration = TD;
        TypedefTypeDeclaration.TypeName =
            "typedef " + ParameterType.getAsString();
        *isResolved = false;
        dependencyList->push_back("typedef " + ParameterType.getAsString());

        if (utils->isNewType(ParameterType)) {
          utils->getDeclsToFind()->push(TypedefTypeDeclaration);
        }
      }
      DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
    } else if (const RecordType *RT = ParameterType->getAs<RecordType>()) {

      if (utils->isOnBlacklist(ParameterType.getAsString())) {
        utils->getResolvedDecls()->insert(ParameterType.getAsString());
        DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
      } else {
        RecordDecl *RD = RT->getDecl();

        if (RD->getNameAsString() == "") {
          std::string AnonRecordDeclaration;

          if (RT->isStructureType()) {
            AnonRecordDeclaration += "struct ";
          } else if (RT->isUnionType()) {
            AnonRecordDeclaration += "union ";
          }

          AnonRecordDeclaration +=
              FFIBindingsUtils::getInstance()->getDeclAttrs(RD);
          AnonRecordDeclaration += "{\n";

          for (RecordDecl::field_iterator FI = RD->field_begin();
               FI != RD->field_end(); ++FI) {
            std::string FieldDeclaration = FI->getNameAsString();
            RecordsVisitor.checkFieldType(FI->getType(), isResolved,
                                          dependencyList, FieldDeclaration);
            AnonRecordDeclaration += FieldDeclaration + ";\n";
          }

          AnonRecordDeclaration += "}";

          DeclarationCore = AnonRecordDeclaration + " " + DeclarationCore;

        } else {
          *isResolved = false;
          dependencyList->push_back(ParameterType.getAsString());

          if (utils->isNewType(ParameterType)) {
            TypeDeclaration RecordTypeDeclaration;
            RecordTypeDeclaration.Declaration = RD;
            RecordTypeDeclaration.TypeName = ParameterType.getAsString();

            utils->getDeclsToFind()->push(RecordTypeDeclaration);
          }
          DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
        }
      }

    } else if (const EnumType *ET = ParameterType->getAs<EnumType>()) {

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
              AnonEnumDeclaration += elements[i] + "}";
            }
          }
          DeclarationCore = AnonEnumDeclaration + DeclarationCore;

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
            DeclarationCore =
                ParamTypeFull.getAsString() + " " + DeclarationCore;
          }
        }
      }
    } else if (ParameterType->isFunctionPointerType()) {
      std::string FunctionPointerDeclarationCore;
      const FunctionProtoType *FPT =
          (const FunctionProtoType *)
          ParameterType->getPointeeType()->getAs<FunctionType>();
      std::string ReturnValueDeclaration;
      FunctionsVisitor.checkParameterType(
          FPT->getReturnType(), isResolved, dependencyList,
          ReturnValueDeclaration, FunctionsVisitor.RETVAL);
      DeclarationCore =
          ReturnValueDeclaration + " (*" + DeclarationCore + ")" + "(";
      unsigned int NumOfParams = FPT->getNumParams();
      for (unsigned int i = 0; i < NumOfParams; i++) {
        std::string ParameterDeclaration;
        FunctionsVisitor.checkParameterType(
            FPT->getParamType(i), isResolved, dependencyList,
            ParameterDeclaration, FunctionsVisitor.PARAM);
        FunctionPointerDeclarationCore += ParameterDeclaration;
        if (i != NumOfParams - 1) {
          FunctionPointerDeclarationCore += ", ";
        }
      }
      if (FPT->isVariadic()) {
        FunctionPointerDeclarationCore += ", ...";
      }
      FunctionPointerDeclarationCore += ")";
      DeclarationCore += FunctionPointerDeclarationCore;
    } else if (ParameterType->isPointerType()) {
      DeclarationCore = "(*" + DeclarationCore + ")";
      checkPointerType(ParameterType->getPointeeType(), isResolved,
                       dependencyList, DeclarationCore);
    } else if (ParameterType->isArrayType()) {

      if (ParameterType->isConstantArrayType()) {
        std::string ArrayDeclaration =
            utils->getArraySize(Context->getAsConstantArrayType(ParameterType));
        DeclarationCore = DeclarationCore + ArrayDeclaration;
        checkPointerType(
            Context->getAsConstantArrayType(ParameterType)->getElementType(),
            isResolved, dependencyList, DeclarationCore);
      }
    } else if (ParameterType->isFundamentalType()) {
      DeclarationCore = ParamTypeFull.getAsString() + " " + DeclarationCore;
    } else if (const VectorType *VT = ParameterType->getAs<VectorType>()) {
      if (VT->getVectorKind() == VectorType::GenericVector &&
          VT->getElementType()->isFundamentalType()) {
        DeclarationCore = VT->getElementType().getAsString() + " " +
                          DeclarationCore + " __attribute__((__vector_size__(" +
                          std::to_string(VT->getNumElements()) + " * sizeof(" +
                          VT->getElementType().getAsString() + "))))";
      }
    }
  }

  void resolveAnonRecord(RecordDecl *RD) {

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
    for (std::vector<std::string>::iterator i =
             FFIBindingsUtils::getInstance()->getAnonymousRecords()->begin();
         i != FFIBindingsUtils::getInstance()->getAnonymousRecords()->end();
         ++i) {
      if (*i == AnonRecordName) {
        distance = std::distance(
            FFIBindingsUtils::getInstance()->getAnonymousRecords()->begin(), i);
        break;
      }
      j++;
    }
    if (j == FFIBindingsUtils::getInstance()->getAnonymousRecords()->size()) {
      FFIBindingsUtils::getInstance()->getAnonymousRecords()->push_back(
          AnonRecordName);
      distance =
          FFIBindingsUtils::getInstance()->getAnonymousRecords()->size() - 1;
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
    std::replace(AnonRecordName.begin(), AnonRecordName.end(), '-', '_');
    AnonRecordName =
        "Anonymous_" + AnonRecordName + "_" + std::to_string(distance);

    std::string attrs = FFIBindingsUtils::getInstance()->getDeclAttrs(RD);

    if (RD->getTypeForDecl()->isStructureType()) {
      RecordDeclaration = "struct " + attrs + AnonRecordName;
      AnonRecordName = "struct " + AnonRecordName;
    } else if (RD->getTypeForDecl()->isUnionType()) {
      RecordDeclaration = "union " + attrs + AnonRecordName;
      AnonRecordName = "union " + AnonRecordName;
    }

    RecordDeclaration += " {\n";
    // check the fields
    for (RecordDecl::field_iterator FI = RD->field_begin();
         FI != RD->field_end(); ++FI) {
      std::string FieldDeclaration = FI->getNameAsString();
      RecordsVisitor.checkFieldType(FI->getType(), &isResolved, dependencyList,
                                    FieldDeclaration);
      RecordDeclaration += FieldDeclaration + ";\n";
    }
    RecordDeclaration += "};\n";

    if (isResolved) {
      FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
          AnonRecordName);
      output += RecordDeclaration;
      output += "\n";
      delete dependencyList;
    } else {
      // add this record to list of unresolved declarations
      DeclarationInfo RecordDeclarationInfo;
      RecordDeclarationInfo.isResolved = isResolved;
      RecordDeclarationInfo.dependencyList = dependencyList;
      RecordDeclarationInfo.Declaration = RecordDeclaration;

      std::pair<std::string, DeclarationInfo> Record(AnonRecordName,
                                                     RecordDeclarationInfo);
      FFIBindingsUtils::getInstance()->getUnresolvedDeclarations()->insert(
          Record);
    }
  }

  /**
   * Determine whether this declaration depends on itself (directly or
   * indirectly).
   */
  bool DependsOnItself(std::string DeclName,
                       std::vector<std::string> *dependencyList) {
    // Direct and indirect dependencies of this declaration
    std::stack<std::string> Dependencies;
    // Declarations that have already been checked
    std::set<std::string> CheckedDecls;

    for (std::vector<std::string>::iterator dependency =
             dependencyList->begin();
         dependency != dependencyList->end(); ++dependency) {
      if (utils->isInUnresolvedDeclarations(*dependency)) {
        Dependencies.push(*dependency);
        CheckedDecls.insert(*dependency);
      }
    }
    while (Dependencies.size()) {
      std::string DeclToCheck = Dependencies.top();
      Dependencies.pop();
      if (DeclToCheck == DeclName) {
        return true;
      }
      DeclarationInfo depDecl =
          utils->getUnresolvedDeclarations()->at(DeclToCheck);
      if (!utils->isInResolvedDecls(DeclToCheck) &&
          !DependsOnItselfDirectly(DeclToCheck, depDecl.dependencyList)) {
        for (std::vector<std::string>::iterator dependency =
                 depDecl.dependencyList->begin();
             dependency != depDecl.dependencyList->end(); ++dependency) {
          if (utils->isInUnresolvedDeclarations(*dependency) &&
              (CheckedDecls.find(*dependency) == CheckedDecls.end())) {
            Dependencies.push(*dependency);
            CheckedDecls.insert(*dependency);
          }
        }
      }
    }
    return false;
  }

  /**
   * Determine whether this declaration depends on itself directly.
   */
  bool DependsOnItselfDirectly(std::string DeclName,
                               std::vector<std::string> *dependencyList) {
    for (std::vector<std::string>::iterator dependency =
             dependencyList->begin();
         dependency != dependencyList->end(); ++dependency) {
      if (*dependency == DeclName) {
        return true;
      }
    }
    return false;
  }
};

class GenerateFFIBindingsAction : public PluginASTAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef inputFile) {

    FFIBindingsUtils *utils = FFIBindingsUtils::getInstance();

    if (utils->getOutputFileName() == "") {
      std::string filename = inputFile;
      char separator;
#ifdef LLVM_ON_UNIX
      separator = '/';
#else
      separator = '\\';
#endif
      if (filename.find_last_of(separator) != std::string::npos) {
        filename = filename.substr(filename.find_last_of(separator) + 1);
      }
      filename.replace(filename.find_first_of('.'),
                       filename.length() - filename.find_first_of('.'), "");
      filename += "_gen_ffi.lua";
      utils->setOutputFileName(filename);
    }
    return llvm::make_unique<GenerateFFIBindingsConsumer>();
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) {

    FFIBindingsUtils *utils = FFIBindingsUtils::getInstance();

    for (unsigned i = 0, e = args.size(); i != e; ++i) {

      if (args[i] == "help") {
        PrintHelp(llvm::errs());
        return true;
      }

      if (args[i] == "test") {
        utils->setTestingMode(true);
      }

      if (args[i] == "-output") {
        if (args.size() >= i + 2) {
          utils->setOutputFileName(args[i + 1]);
        } else {
          llvm::outs() << "Enter output file name.\n";
        }
      }

      if (args[i] == "-header") {
        if (args.size() >= i + 2) {
          utils->setHeaderFileName(args[i + 1]);
        } else {
          llvm::outs() << "Enter header file name.\n";
        }
      }

      if (args[i] == "-blacklist") {
        if (args.size() >= i + 2) {
          utils->setBlacklistFileName(args[i + 1]);
        } else {
          llvm::outs()
              << "Enter name of the file containing type blacklist. \n";
        }
      }

      if (args[i] == "-destdir") {
        if (args.size() >= i + 2) {
          char separator;
#ifdef LLVM_ON_UNIX
          separator = '/';
#else
          separator = '\\';
#endif
          std::string destDir = args[i + 1];
          if (destDir.size() > 0) {
            int end = destDir.size() - 1;
            if (destDir[end] != separator) {
              destDir += separator;
            }
          }
          utils->setDestinationDirectory(destDir);
        } else {
          llvm::outs() << "Enter path of the destination directory.\n";
        }
      }
    }
    return true;
  }
  void PrintHelp(llvm::raw_ostream &ros) {
    ros << "----------------------------------\n";
    ros << "---------- ffi-gen help ----------\n";
    ros << "----------------------------------\n\n";
    ros << "Options:\n";
    ros << "  -output    Specifies output file (generated Lua file). Default "
           "file name is \"output.lua\".\n";
    ros << "  -header    Specifies text file that contains a header to put in "
           "the generated file.\n";
    ros << "  -blacklist    Specifies text file that contains list of types "
           "that should not be emitted or resolved.\n";
    ros << "  -destdir    Specifies path to the destination directory. This is "
           "the directory where output Lua file will be generated.\n";
    ros << "   test      Turns on test mode. When in test mode,\n"
           "             the plugin generates bindings for each function,\n"
           "             whether it was marked with the ffibinding attribute "
           "or not.\n"
           "             When in this mode, name of the generated output file "
           "is\n"
           "             formed from the absolute path of the source file.\n";
    ros << "             To enable test mode add: -Xclang -plugin-arg-ffi-gen "
           "-Xclang test\n";
    ros << "These options and their values must be preceded by "
           "\"-plugin-arg-ffi-gen\" "
           "option.\n\n";

    ros << "Example of running the plugin:\n";
    ros << "<path-to>/clang test.c -c -Xclang -load -Xclang "
           "<path-to>/ffi-gen.so -Xclang -plugin -Xclang ffi-gen "
           "-Xclang -plugin-arg-ffi-gen -Xclang -output -Xclang "
           "-plugin-arg-ffi-gen -Xclang test.lua "
           "-Xclang -plugin-arg-ffi-gen -Xclang -header -Xclang "
           "-plugin-arg-ffi-gen -Xclang test.txt "
           "-Xclang -plugin-arg-ffi-gen -Xclang -blacklist -Xclang "
           "-plugin-arg-ffi-gen -Xclang blacklist.txt\n\n";
    ros << "This will generate LuaJIT ffi bindings for functions, structs and "
           "unions \nmarked with the ffibinding attribute from the input file "
           "\"test.c\" \nand write the generated bindings to a file named "
           "\"test.lua\".\nIt will also copy the contents of the \"test.txt\" "
           "file at the top of the \"test.lua\" file.\nOutput file will not "
           "contain bindings for types that are listed in the "
           "\"blacklist.txt\" "
           "file.\n\n";
  }
};

static FrontendPluginRegistry::Add<GenerateFFIBindingsAction>
X("ffi-gen", "generate LuaJIT ffi bindings");
