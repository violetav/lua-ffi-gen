#ifndef GENERATEFFIBINDINGS_H
#define GENERATEFFIBINDINGS_H

#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include <fstream>
using namespace clang;

namespace constants {
const std::string SRC_FILE_PLACE_HOLDER = "<source-files>";
}

/**
 * Data containing the declaration node (with information attached to it) and
 *additional information.
 **/
struct TypeDeclaration {
  std::string TypeName;
  /** Declaration node. The information that it contains depends on the type of
   * the declaration (function, struct, enum, etc.). */
  TypeDecl *Declaration;
};

/**
 * Information about a declaration needed for it to be resolved and
 * printed in the right order (after declarations that it depends on).
 *
 **/
struct DeclarationInfo {
  /** Full declaration (this is what will be printed). */
  std::string Declaration;
  /** Is the declaration already resolved. */
  bool isResolved = true;
  /** A list of declarations this declaration depends on (these declarations
   * need to be printed out before it). */
  std::vector<std::string> *dependencyList;
};

/**
 * Finds specified functions and gathers data about them that's needed
 * to resolve them (print them out).
 **/
class FunctionVisitor : public RecursiveASTVisitor<FunctionVisitor> {
public:
  FunctionVisitor() {}
  /** Handles the function parameter type and modifies information about
   *  the function declaration (adds declarations that it depends on, if there
   * are any, etc.). */
  enum Type {
    RETVAL,
    PARAM
  };

  /** Visits function declarations in a parsed AST. */
  bool VisitFunctionDecl(FunctionDecl *FD);
  /** Set output string for writing output to a file. */
  void setOutput(std::string *output_);
  static void checkParameterType(QualType ParameterType, bool *isResolved,
                                 std::vector<std::string> *dependencyList,
                                 std::string &DeclarationCore, enum Type type);

private:
  static ASTContext *Context;
  /** Output string for writing the output to a file. */
  std::string *output;
};

/**
 * Finds records (struct, union) declarations and gathers data about them
 * that's needed to resolve them (print them out).
 **/
class RecordVisitor : public RecursiveASTVisitor<RecordVisitor> {
public:
  RecordVisitor() {}
  /** Visits record declarations in a parsed AST. */
  bool VisitRecordDecl(RecordDecl *RD);
  /** Tries to resolve given record declaration. If it can be immediately
   * resolved it is printed out, otherwise further actions that are needed for
   * it to be resolved are done. */
  void findRecordDeclaration(RecordDecl *RD);
  /** Handles the record's field type and modifies information about
   *  the record declaration (adds declarations that it depends on, if there are
   * any, etc.). */
  void checkFieldType(QualType FieldType, bool *isResolved,
                      std::vector<std::string> *dependencyList,
                      std::string &RecordDeclaration);
  /** Set output string for writing output to a file. */
  void setOutput(std::string *output_);
  void setContext(ASTContext *astContext) { Context = astContext; }

private:
  ASTContext *Context;
  /** Output string for writing the output to a file. */
  std::string *output;
};

/**
 * Finds enumerations marked with the ffibinding attribute and prints them out.
 **/
class EnumVisitor : public RecursiveASTVisitor<EnumVisitor> {
public:
  EnumVisitor() {}
  /** Visits enum declarations in a parsed AST. */
  bool VisitEnumDecl(EnumDecl *ED);
  /** Set output string for writing output to a file. */
  void setOutput(std::string *output_);
  void setContext(ASTContext *astContext) { Context = astContext; }

private:
  ASTContext *Context;
  /** Output string for writing the output to a file. */
  std::string *output;
};

class FFIBindingsUtils {
public:
  static FFIBindingsUtils *getInstance();
  /** Is this the first time to come across given declaration type.
   *  Returns false if the type is already resolved, waiting to be resolved or
   * printed out, true otherwise. */
  bool isNewType(QualType Type);
  /** Returns true if given declaration type is in the UnresolvedDeclarations
   * list, false otherwise. */
  bool isInUnresolvedDeclarations(std::string DeclType);
  /** Returns true if given declaration type is in the ResolvedDecls
   * list, false otherwise. */
  bool isInResolvedDecls(std::string DeclType);
  /** Returns true if this type should not be resolved and emitted because it is
   * on the blacklist, false otherwise. */
  bool isOnBlacklist(std::string DeclType);

  /** Used for handling multi-dimensional arrays.
   *  As long as there are more dimensions found, they are appended to the array
   * declaration.
   *  Return value is the type of the array elements. */
  QualType handleMultiDimArrayType(ASTContext *Context, QualType ArrayType,
                                   std::string &ArrayDeclaration);
  /** Get size of the array (e.g. for "double arr[6]" return value would be
   * "[6]"). */
  std::string getArraySize(const ConstantArrayType *CAT);
  /** Returns a string containing the list of attributes attached to this
   * Decl.*/
  std::string getDeclAttrs(Decl *RD);

  std::map<std::string, DeclarationInfo> *getUnresolvedDeclarations() {
    return UnresolvedDeclarations;
  }

  void setUnresolvedDeclarations(
      std::map<std::string, DeclarationInfo> *UnresolvedDeclarations_) {
    UnresolvedDeclarations = UnresolvedDeclarations_;
  }

  std::stack<TypeDeclaration> *getDeclsToFind() { return DeclsToFind; }

  void setDeclsToFind(std::stack<TypeDeclaration> *DeclsToFind_) {
    DeclsToFind = DeclsToFind_;
  }

  std::set<std::string> *getResolvedDecls() { return ResolvedDecls; }

  void setResolvedDecls(std::set<std::string> *ResolvedDecls_) {
    ResolvedDecls = ResolvedDecls_;
  }

  std::string getOutputFileName() { return outputFileName; }

  void setOutputFileName(std::string filename) { outputFileName = filename; }

  std::string getHeaderFileName() { return headerFileName; }

  void setHeaderFileName(std::string filename) { headerFileName = filename; }

  std::string getBlacklistFileName() { return blacklistFileName; }

  void setBlacklistFileName(std::string filename) {
    blacklistFileName = filename;
  }

  std::string getDestinationDirectory() { return destinationDirectory; }

  void setDestinationDirectory(std::string destDir) {
    destinationDirectory = destDir;
  }

  std::set<std::string> *getBlacklist() { return blacklist; }

  void setBlacklist(std::set<std::string> *list) { blacklist = list; }

  bool isTestingModeOn() { return isTestingMode; }

  void setTestingMode(bool testingMode) { isTestingMode = testingMode; }

  std::vector<std::string> *getAnonymousRecords() { return AnonymousRecords; }

  ~FFIBindingsUtils() {
    delete UnresolvedDeclarations;
    delete DeclsToFind;
    delete ResolvedDecls;
    delete blacklist;
    delete AnonymousRecords;
  }

  bool hasMarkedDeclarations() { return markedDeclarations; }

  void setHasMarkedDeclarations(bool markedDeclarations_) {
    markedDeclarations = markedDeclarations_;
  }

private:
  static FFIBindingsUtils *instance;
  FFIBindingsUtils() {
    UnresolvedDeclarations = new std::map<std::string, DeclarationInfo>();
    DeclsToFind = new std::stack<TypeDeclaration>();
    ResolvedDecls = new std::set<std::string>();
    blacklist = new std::set<std::string>();
    AnonymousRecords = new std::vector<std::string>();
  }
  FFIBindingsUtils(FFIBindingsUtils &);
  FFIBindingsUtils &operator=(FFIBindingsUtils &);

  /** A map containing pairs of declaration names (types) and additional
   *  information about them (e.g. a list of declarations they depend on).
   *  This map contains declarations that cannot be resolved immediately
   *  (contain non-primitive types). */
  std::map<std::string, DeclarationInfo> *UnresolvedDeclarations;
  /** A list of declarations that need to be found. */
  std::stack<TypeDeclaration> *DeclsToFind;
  /** A list of resolved (printed out) declarations. */
  std::set<std::string> *ResolvedDecls;

  std::string outputFileName = "";
  std::string headerFileName = "";
  std::string blacklistFileName = "";
  std::string destinationDirectory = "";
  std::set<std::string> *blacklist;
  bool isTestingMode = false;
  std::vector<std::string> *AnonymousRecords;
  bool markedDeclarations = false;
};
#endif /* GENERATEFFIBINDINGS_H */
