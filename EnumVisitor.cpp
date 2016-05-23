#include "GenerateFFIBindings.hpp"

void EnumVisitor::setOutput(std::string *output_) { output = output_; }

bool EnumVisitor::VisitEnumDecl(EnumDecl *ED) {

  if (ED->hasAttr<FFIBindingAttr>()) {
    FFIBindingsUtils::getInstance()->setHasMarkedDeclarations(true);

    if (FFIBindingsUtils::getInstance()->isOnBlacklist("enum " +
                                                       ED->getNameAsString()) ||
        FFIBindingsUtils::getInstance()->isInResolvedDecls(
            "enum " + ED->getNameAsString())) {
      return true;
    }
    std::string EnumDeclaration;
    std::vector<std::string> elements;

    std::string attrs = FFIBindingsUtils::getInstance()->getDeclAttrs(ED);
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
      if (i < length - 1) {
        EnumDeclaration += elements[i] + ", ";
      } else {
        EnumDeclaration += elements[i] + "};\n";
      }
    }

    (*output) += EnumDeclaration; // print the enumeration
    (*output) += "\n";
    FFIBindingsUtils::getInstance()->getResolvedDecls()->insert(
        "enum " + ED->getNameAsString());
  }
  return true;
}
