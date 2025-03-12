#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <float.h>
#include "xml.h"
 
/*******************/
/* XML File Parser */
/*******************/
flagcxResult_t xmlGetChar(FILE* file, char* c) {
  if (fread(c, 1, 1, file) == 0) {
    WARN("XML Parse : Unexpected EOF");
    return flagcxInternalError;
  }
  return flagcxSuccess;
}

flagcxResult_t xmlGetValue(FILE* file, char* value, char* last) {
  char c;
  FLAGCXCHECK(xmlGetChar(file, &c));
  if (c != '"' && c != '\'') {
#if INT_OK
    int o = 0;
    do {
      value[o++] = c;
      FLAGCXCHECK(xmlGetChar(file, &c));
    } while (c >= '0' && c <= '9');
    value[o] = '\0';
    *last = c;
    return flagcxSuccess;
#else
    WARN("XML Parse : Expected (double) quote.");
    return flagcxInternalError;
#endif
  }
  int o = 0;
  do {
    FLAGCXCHECK(xmlGetChar(file, &c));
    value[o++] = c;
  } while (c != '"');
  value[o-1] = '\0';
  FLAGCXCHECK(xmlGetChar(file, last));
  return flagcxSuccess;
}

flagcxResult_t xmlGetToken(FILE* file, char* name, char* value, char* last) {
  char c;
  char* ptr = name;
  int o = 0;
  do {
    FLAGCXCHECK(xmlGetChar(file, &c));
    if (c == '=') {
      ptr[o] = '\0';
      if (value == NULL) {
	      WARN("XML Parse : Unexpected value with name %s", ptr);
	      return flagcxInternalError;
      }
      return xmlGetValue(file, value, last);
    }
    ptr[o] = c;
    if (o == MAX_STR_LEN-1) {
      ptr[o] = '\0';
      WARN("Error : name %s too long (max %d)", ptr, MAX_STR_LEN);
      return flagcxInternalError;
    }
    o++;
  } while (c != ' ' && c != '>' && c != '/' && c != '\n' && c != '\r');
  ptr[o-1] = '\0';
  *last = c;
  return flagcxSuccess;
}

// Shift the 3-chars string by one char and append c at the end
#define SHIFT_APPEND(s, c) do { s[0]=s[1]; s[1]=s[2]; s[2]=c; } while(0)
flagcxResult_t xmlSkipComment(FILE* file, char* start, char next) {
  // Start from something neutral with \0 at the end.
  char end[4] = "...";
 
  // Inject all trailing chars from previous reads. We don't need
  // to check for --> here because there cannot be a > in the name.
  for (int i=0; i<strlen(start); i++) SHIFT_APPEND(end, start[i]);
  SHIFT_APPEND(end, next);
 
  // Stop when we find "-->"
  while (strcmp(end, "-->") != 0) {
    int c;
    if (fread(&c, 1, 1, file) != 1) {
      WARN("XML Parse error : unterminated comment");
      return flagcxInternalError;
    }
    SHIFT_APPEND(end, c);
  }
  return flagcxSuccess;
}
 
flagcxResult_t xmlGetNode(FILE* file, struct flagcxXmlNode* node) {
  node->type = NODE_TYPE_NONE;
  char c = ' ';
  while (c == ' ' || c == '\n' || c == '\r') {
    if (fread(&c, 1, 1, file) == 0) return flagcxSuccess;
  }
  if (c != '<') {
    WARN("XML Parse error : expecting '<', got '%c'", c);
    return flagcxInternalError;
  }
  // Read XML element name
  FLAGCXCHECK(xmlGetToken(file, node->name, NULL, &c));
 
  // Check for comments
  if (strncmp(node->name, "!--", 3) == 0) {
    FLAGCXCHECK(xmlSkipComment(file, node->name+3, c));
    return xmlGetNode(file, node);
  }
 
  // Check for closing tag
  if (node->name[0] == '\0' && c == '/') {
    node->type = NODE_TYPE_CLOSE;
    // Re-read the name, we got '/' in the first call
    FLAGCXCHECK(xmlGetToken(file, node->name, NULL, &c));
    if (c != '>') {
      WARN("XML Parse error : unexpected trailing %c in closing tag %s", c, node->name);
      return flagcxInternalError;
    }
    return flagcxSuccess;
  }
 
  node->type = NODE_TYPE_OPEN;
 
  // Get Attributes
  int a = 0;
  while (c == ' ') {
    FLAGCXCHECK(xmlGetToken(file, node->attrs[a].key, node->attrs[a].value, &c));
    if (a == MAX_ATTR_COUNT) {
      INFO(FLAGCX_GRAPH, "XML Parse : Ignoring extra attributes (max %d)", MAX_ATTR_COUNT);
      // Actually we need to still consume the extra attributes so we have an extra one.
    } else a++;
  }
  node->nAttrs = a;
  if (c == '/') {
    node->type = NODE_TYPE_SINGLE;
    char str[MAX_STR_LEN];
    FLAGCXCHECK(xmlGetToken(file, str, NULL, &c));
  }
  if (c != '>') {
    WARN("XML Parse : expected >, got '%c'", c);
    return flagcxInternalError;
  }
  return flagcxSuccess;
}

typedef flagcxResult_t (*xmlHandlerFunc_t)(FILE*, struct flagcxXml*, struct flagcxXmlNode*);

struct xmlHandler {
  const char * name;
  xmlHandlerFunc_t func;
};

flagcxResult_t xmlLoadSub(FILE* file, struct flagcxXml* xml, struct flagcxXmlNode* head, struct xmlHandler handlers[], int nHandlers) {
  if (head && head->type == NODE_TYPE_SINGLE) return flagcxSuccess;
  while (1) {
    if (xml->maxIndex == xml->maxNodes) {
      WARN("Error : XML parser is limited to %d nodes", xml->maxNodes);
      return flagcxInternalError;
    }
    struct flagcxXmlNode* node = xml->nodes+xml->maxIndex;
    memset(node, 0, sizeof(struct flagcxXmlNode));
    FLAGCXCHECK(xmlGetNode(file, node));
    if (node->type == NODE_TYPE_NONE) {
      if (head) {
	      WARN("XML Parse : unterminated %s", head->name);
	      return flagcxInternalError;
      } else {
	      // All done
	      return flagcxSuccess;
      }
    }
    if (head && node->type == NODE_TYPE_CLOSE) {
      if (strcmp(node->name, head->name) != 0) {
	      WARN("XML Mismatch : %s / %s", head->name, node->name);
      	return flagcxInternalError;
      }
      return flagcxSuccess;
    }
    int found = 0;
    for (int h=0; h<nHandlers; h++) {
      if (strcmp(node->name, handlers[h].name) == 0) {
	      if (head) {
      	  if (head->nSubs == MAX_SUBS) {
	          WARN("Error : XML parser is limited to %d subnodes", MAX_SUBS);
      	    return flagcxInternalError;
      	  }
      	  head->subs[head->nSubs++] = node;
	      }
	      node->parent = head;
	      node->nSubs = 0;
	      xml->maxIndex++;
	      FLAGCXCHECK(handlers[h].func(file, xml, node));
	      found = 1;
	      break;
      }
    }
    if (!found) {
      if (nHandlers) INFO(FLAGCX_GRAPH, "Ignoring element %s", node->name);
      FLAGCXCHECK(xmlLoadSub(file, xml, node, NULL, 0));
    }
  }
}

/****************************************/
/* Parser rules for our specific format */
/****************************************/
flagcxResult_t flagcxTopoXmlLoadGpu(FILE* file, struct flagcxXml* xml, struct flagcxXmlNode* head) {
  FLAGCXCHECK(xmlLoadSub(file, xml, head, NULL, 0));
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoXmlLoadSystem(FILE* file, struct flagcxXml* xml, struct flagcxXmlNode* head) {
  int version;
  FLAGCXCHECK(xmlGetAttrInt(head, "version", &version));
  if (version != FLAGCX_TOPO_XML_VERSION) {
    WARN("XML Topology has wrong version %d, %d needed", version, FLAGCX_TOPO_XML_VERSION);
    return flagcxInvalidUsage;
  }
  const char* name;
  FLAGCXCHECK(xmlGetAttr(head, "name", &name));
  if (name != NULL) INFO(FLAGCX_GRAPH, "Loading topology %s", name);
  else INFO(FLAGCX_GRAPH, "Loading unnamed topology");
 
  struct xmlHandler handlers[] = { { "gpu", flagcxTopoXmlLoadGpu } };
  FLAGCXCHECK(xmlLoadSub(file, xml, head, handlers, 1));
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoGetXmlFromFile(const char* xmlTopoFile, struct flagcxXml* xml, int warn) {
  FILE* file = fopen(xmlTopoFile, "r");
  if (file == NULL) {
    if (warn) {
      WARN("Could not open XML topology file %s : %s", xmlTopoFile, strerror(errno));
    }
    return flagcxSuccess;
  }
  INFO(FLAGCX_GRAPH, "Loading topology file %s", xmlTopoFile);
  struct xmlHandler handlers[] = { { "system", flagcxTopoXmlLoadSystem } };
  xml->maxIndex = 0;
  FLAGCXCHECK(xmlLoadSub(file, xml, NULL, handlers, 1));
  fclose(file);
  return flagcxSuccess;
}