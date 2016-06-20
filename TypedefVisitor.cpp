#include "GenerateFFIBindings.hpp"

bool TypedefVisitor::VisitTypedefDecl(TypedefNameDecl *TD) {
  if (!TD->hasAttr<FFIBindingAttr>())
    return true;

  if (FFIBindingsUtils::getInstance()->isInResolvedDecls("typedef " +
                                                         TD->getNameAsString()))
    return true;

  if (FFIBindingsUtils::getInstance()->isOnBlacklist(TD->getNameAsString())) {
    FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
        "typedef " + TD->getNameAsString());
    return true;
  }

  FFIBindingsUtils::getInstance()->setHasMarkedDeclarations(true);
  FFIBindingsUtils::getInstance()->resolveTypedefDecl(TD);

  return true;
}
