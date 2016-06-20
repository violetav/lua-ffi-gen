#include "GenerateFFIBindings.hpp"

bool EnumVisitor::VisitEnumDecl(EnumDecl *ED) {
  if (!ED->hasAttr<FFIBindingAttr>())
    return true;

  if (FFIBindingsUtils::getInstance()->isInResolvedDecls("enum " +
                                                         ED->getNameAsString()))
    return true;

  if (FFIBindingsUtils::getInstance()->isOnBlacklist("enum " +
                                                     ED->getNameAsString())) {
    FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
        "enum " + ED->getNameAsString());
    return true;
  }

  FFIBindingsUtils::getInstance()->setHasMarkedDeclarations(true);
  FFIBindingsUtils::getInstance()->resolveEnumDecl(ED);

  return true;
}
