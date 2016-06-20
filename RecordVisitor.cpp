#include "GenerateFFIBindings.hpp"

bool RecordVisitor::VisitRecordDecl(RecordDecl *RD) {
  // try to resolve record declaration if it has ffibinding attribute
  if (!RD->hasAttr<FFIBindingAttr>())
    return true;

  // if this record type has already been resolved, then there's nothing to do
  if (!FFIBindingsUtils::getInstance()->isNewType(
           RD->getTypeForDecl()->getCanonicalTypeInternal()))
    return true;

  FFIBindingsUtils::getInstance()->setHasMarkedDeclarations(true);
  FFIBindingsUtils::getInstance()->resolveRecordDecl(RD);

  return true;
}
