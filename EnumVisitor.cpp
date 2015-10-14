#include "GenerateFFIBindings.hpp"

void EnumVisitor::setOutput(llvm::raw_fd_ostream *output_) { output = output_; }

bool EnumVisitor::VisitEnumDecl(EnumDecl *ED) {

  if (ED->hasAttr<FFIBindingAttr>()) {

    if (FFIBindingsUtils::getInstance()->isOnBlacklist("enum " +
                                                       ED->getNameAsString()) ||
        FFIBindingsUtils::getInstance()->isInResolvedDecls(
            "enum " + ED->getNameAsString())) {
      return true;
    }
    std::string EnumDeclaration;
    std::vector<std::string> elements;

    EnumDeclaration = "enum " + ED->getNameAsString() + " {";

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

    (*output) << EnumDeclaration; // print the enumeration
    (*output) << "\n";
    FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
        "enum " + ED->getNameAsString());
  }
  return true;
}
