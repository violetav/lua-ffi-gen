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
    if (sourceFileName.find_last_of(separator) != std::string::npos)
      sourceFileName =
          sourceFileName.substr(sourceFileName.find_last_of(separator) + 1);

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
          if (line.find(constants::SRC_FILE_PLACE_HOLDER) != std::string::npos)
            line.replace(line.find(constants::SRC_FILE_PLACE_HOLDER),
                         constants::SRC_FILE_PLACE_HOLDER.length(),
                         sourceFileName);
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

    FFIBindingsUtils::getInstance()->setOutput(&output);
    FFIBindingsUtils::getInstance()->setContext(&context);

    // visit all function declarations and extract information
    // about unresolved dependencies, if there are any
    FunctionsVisitor.TraverseDecl(context.getTranslationUnitDecl());
    // visit all record declarations that are marked with ffibinding attribute
    // and start resolving them
    RecordsVisitor.TraverseDecl(context.getTranslationUnitDecl());
    // visit all enum declarations that are marked with ffibinding attribute
    // and print them out
    EnumsVisitor.TraverseDecl(context.getTranslationUnitDecl());
    // visit all typedef declarations that are marked with ffibinding attribute
    // and start resolving them
    TypedefsVisitor.TraverseDecl(context.getTranslationUnitDecl());

    // go through DeclsToFind until all required declarations are found
    while (utils->getDeclsToFind()->size() > 0) {

      TypeDeclaration Decl = utils->getDeclsToFind()->top();
      utils->getDeclsToFind()->pop();

      const Type *DeclType = Decl.Declaration->getTypeForDecl();

      if (!utils->isInResolvedDecls(Decl.TypeName) &&
          !utils->isInUnresolvedDeclarations(Decl.TypeName)) {
        if (DeclType->getAs<TypedefType>())
          FFIBindingsUtils::getInstance()->resolveTypedefDecl(
              (TypedefNameDecl *)Decl.Declaration);
        else if (DeclType->isRecordType()) {
          if (Decl.Declaration->getNameAsString() == "")
            FFIBindingsUtils::getInstance()->resolveAnonRecord(
                (RecordDecl *)Decl.Declaration);
          else
            FFIBindingsUtils::getInstance()->resolveRecordDecl(
                (RecordDecl *)Decl.Declaration);
        } else if (DeclType->isEnumeralType())
          FFIBindingsUtils::getInstance()->resolveEnumDecl(
              (EnumDecl *)Decl.Declaration);
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
          if (utils->isInResolvedDecls(*it1))
            j++;
          else
            break;
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
      if (!printedSomething)
        possibleLoop = true;
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
  TypedefVisitor TypedefsVisitor;
  std::string output;
  FFIBindingsUtils *utils;

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
      if (DeclToCheck == DeclName)
        return true;
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
      if (*dependency == DeclName)
        return true;
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
      if (filename.find_last_of(separator) != std::string::npos)
        filename = filename.substr(filename.find_last_of(separator) + 1);

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

      if (args[i] == "test")
        utils->setTestingMode(true);

      if (args[i] == "-output") {
        if (args.size() >= i + 2)
          utils->setOutputFileName(args[i + 1]);
        else
          llvm::outs() << "Enter output file name.\n";
      }

      if (args[i] == "-header") {
        if (args.size() >= i + 2)
          utils->setHeaderFileName(args[i + 1]);
        else
          llvm::outs() << "Enter header file name.\n";
      }

      if (args[i] == "-blacklist") {
        if (args.size() >= i + 2)
          utils->setBlacklistFileName(args[i + 1]);
        else
          llvm::outs()
              << "Enter name of the file containing type blacklist. \n";
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
            if (destDir[end] != separator)
              destDir += separator;
          }
          utils->setDestinationDirectory(destDir);
        } else
          llvm::outs() << "Enter path of the destination directory.\n";
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
