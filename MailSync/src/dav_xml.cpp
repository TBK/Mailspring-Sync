#include "mailsync/dav_xml.hpp"
#include "mailsync/sync_exception.hpp"

DavXML::DavXML(std::string xml, std::string url):
    xpathContext(nullptr)
{
    doc = xmlReadMemory(xml.c_str(), (int)xml.size(), url.c_str(), "utf-8", 0);
    if (doc == nullptr) {
        throw new SyncException("Unable to parse CalDav XML", xml, false);
    }
}

DavXML::~DavXML() noexcept // nothrow
{
    if (doc != nullptr) {
        xmlFreeDoc(doc);
    }
    if (xpathContext != nullptr) {
        xmlXPathFreeContext(xpathContext);
    }
}

void DavXML::evaluateXPath(std::string expr, std::function<void(xmlNodePtr)> yieldBlock, xmlNodePtr withinNode)
{
    if (xpathContext == nullptr) {
        xpathContext = xmlXPathNewContext(doc);
        if (xpathContext == NULL) {
            fprintf(stderr,"Error: unable to create new XPath context\n");
            return;
        }

        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"d", (const xmlChar *)"DAV:");
        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"D", (const xmlChar *)"DAV:");
        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"caldav", (const xmlChar *)"urn:ietf:params:xml:ns:caldav");
        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"carddav", (const xmlChar *)"urn:ietf:params:xml:ns:carddav");
        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"cs", (const xmlChar *)"http://calendarserver.org/ns/");
        xmlXPathRegisterNs(xpathContext, (const xmlChar *)"ical", (const xmlChar *)"http://apple.com/ns/ical/");
    }

    xmlXPathObjectPtr xpathObj = nullptr;

    if (withinNode == nullptr) {
        xpathObj = xmlXPathEvalExpression((const xmlChar *)expr.c_str(), xpathContext);
    } else {
        xpathContext->node = withinNode;
        xpathObj = xmlXPathEvalExpression((const xmlChar *)expr.c_str(), xpathContext);
    }
    if (xpathObj == nullptr) {
        fprintf(stderr,"Error: unable to evaluate xpath expression \"%s\"\n", expr.c_str());
        return;
    }

    auto nodes = xpathObj->nodesetval;
    if (nodes == nullptr) {
        return;
    }

    for (int i = 0; i < nodes->nodeNr; ++i) {
        xmlNodePtr cur = nodes->nodeTab[i];
        if(cur->type == XML_NAMESPACE_DECL) {
            yieldBlock((xmlNodePtr)cur->next);
        } else {
            yieldBlock(cur);
        }
    }

    xmlXPathFreeObject(xpathObj);
}

std::string DavXML::nodeContentAtXPath(std::string expr, xmlNodePtr withinNode) {
    std::string result = "";
    evaluateXPath(expr, ([&](xmlNodePtr cur) {
        result = std::string((char *)cur->content);
        return;
    }), withinNode);
    return result;
}
