#include "DesktopEditor/xml/include/xmlutils.h"
#include <iostream>

int main() {
    XmlUtils::CXmlNode root;
    if (!root.FromXmlString(L"<root><value>\u4e2d\u6587 &amp; XML</value></root>")) return 1;
    XmlUtils::CXmlNode value = root.GetNode(L"value");
    if (value.GetText() != L"\u4e2d\u6587 & XML") return 2;
    std::cout << "KERNEL_XML_CHINESE_ENTITY_SMOKE_PASSED\n";
    return 0;
}
