#include "GenerateFFIBindings.hpp"

bool FunctionVisitor::VisitFunctionDecl(FunctionDecl *FD) {

  if (!FFIBindingsUtils::getInstance()->isTestingModeOn()) {
    if (!FD->hasAttr<FFIBindingAttr>())
      return true;
  }

  if (FFIBindingsUtils::getInstance()->isInResolvedDecls(
          "function " + FD->getQualifiedNameAsString()) ||
      FFIBindingsUtils::getInstance()->isInUnresolvedDeclarations(
          "function " + FD->getQualifiedNameAsString()))
    return true;

  FFIBindingsUtils::getInstance()->setHasMarkedDeclarations(true);
  FFIBindingsUtils::getInstance()->resolveFunctionDecl(FD);

  return true;
}
