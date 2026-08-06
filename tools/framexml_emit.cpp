// Dump the Lua the emitter produces for one interface XML file.
//
//     framexml_emit Data/interface/framexml/floatingchatframe.xml
//
// The client can already write this out for a whole run with
// WOWEE_FRAMEXML_EMIT_DIR, and that needs a run. This is the same emitter over
// one file, which is what most questions actually want: whether an attribute
// survived, what a $parent substituted to, which CreateFrame type a template
// child ended up with.
//
// Written after three separate guesses about why the chat edit box had no
// focus — that parentKey was dropped, that it was dropped for frames as
// opposed to regions, that the edit box was not created as an EditBox. All
// three were wrong, and one command each would have said so:
//
//     framexml_emit .../floatingchatframe.xml | grep -n 'editBox'
//
// A hypothesis about emitted output is cheap to test and expensive to assume.
#include "ui/xml_parser.hpp"
#include "ui/framexml_emitter.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::ifstream in(argv[1]);
    std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    wowee::ui::XmlNode root; std::string err;
    if (!wowee::ui::parseXml(src, root, err)) { std::cerr << "parse: " << err << "\n"; return 1; }
    std::cout << wowee::ui::emitFrameXml(root).lua;
    return 0;
}
