%code top {
#include <string.h>
#include <stdlib.h>

extern int yyerror(const char *p);
}

%code requires {
  struct ASTNode;
}

%union {
  long long valI;
  double valF;
  char *valC;
  struct ASTNode *valN;
}

%locations

%code {
/* Yes, this D file is being included. This is the easiest way I could think of
 * to define the node types once only. The only other way would be to run the
 * parsing file through a C preprocessor, which has obvious downsides.
 */
#include "nodetypes.d"
;
/* Semicolon to avoid a compile error with the enum delcaration for C. Not in
 * the D file because I hate warnings
 */
}

%code provides {
struct ASTNode {
  int type;
  YYSTYPE value;

  int cCap; // The index of the next child that will require a reallocation to initialise
  int cCount; // The index of the child to be initialised next
  struct ASTNode **children;
};

typedef struct ASTNode Node;

Node *parseResult;

int column;

extern Node *nalloc(int children, int type);
}

%define parse.error verbose
%define parse.lac full

%token <valC> ID STRING
%token <valF> FLOAT
%token <valI> INTEGER CHAR

%token DBLCOLON "::" DBLBAR "||" DBLAND "&&" DBLXOR "^^" DBLLEFT "<<" DBLRIGHT ">>" TPLRIGHT ">>>" DBLPLUS "++" DBLMINUS "--" DBLNOT "!!"
%token DBLEQ "==" NOTEQ "!=" GTEQ ">=" LTEQ "<="
%token RSARROW "->" CONSTEQ "#=" RUNK "run" LOADK "load"

%type <valC> LibraryName FilePath BinaryOperator PrefixOperator PostfixOperator
%type <valN> LoadExpression FileLocator NamespaceItem RunDeclaration
%type <valN> FuncDeclaration FuncDefinition FunctionBody ParameterList Parameters Parameter
%type <valN> Type SingleType TupleTypes TupleType FunctionType
%type <valN> BlockStatement Statements Statement
%type <valN> Expression AssignExpression BasicExpression DeclExpression
%type <valN> QualifiedID QualifiedIDPart QualifiedIDWithOperators TypeName BasicValue

%left "||"
%left "^^"
%left "&&"
%left '|'
%left '^'
%left '&'
%left "==" "!="
%left '<' "<=" '>' ">="
%left "<=>"
%left "<<" ">>" ">>>"
%left '+' '-'
%left '*' '/' '%'

%left "PostfixOperator"
%right "PrefixOperator"

%code {
extern void doLog(const char*);

Node *nalloc(int children, int type) {
  Node *n = (Node*) malloc(sizeof(Node));
  n->type = type;
  n->value.valI = 0;
  if (children > 0) n->children = (Node**) malloc(sizeof(Node) * children);
  n->cCap = children;
  n->cCount = 0;
  return n;
}

Node *addChild(Node *parent, Node *child) {
  if (parent->cCount >= parent->cCap) {
    if (parent->cCap <= 0) {
      parent->cCap = 1;
      parent->children = malloc(sizeof(Node**));
    } else {
      parent->cCap *= 2;
      parent->children = realloc(parent->children, parent->cCap);
    }
  }
  parent->children[parent->cCount] = child;
  return parent->children[parent->cCount++]; // ++ doesn't affect result
}
}

%%
top:
    %empty
  | top LoadExpression { addChild(parseResult, $2); }
  | top NamespaceItem  { addChild(parseResult, $2); }
  ;

LoadExpression:
    "load" '(' FileLocator[file] ')' { $$ = nalloc(1, LOAD); addChild($$, $file); }
  ;

FileLocator:
    FilePath[file]                      { $$ = nalloc(1, NONE); addChild($$, nalloc(0, FILEPATH))->value.valC = $file; }
  | LibraryName[lib] ':' FilePath[file] { $$ = nalloc(2, NONE); addChild($$, nalloc(0, LIBNAME))->value.valC = $lib; addChild($$, nalloc(0, FILEPATH))->value.valC = $file; }
  ;

FilePath:
    ID
  | ID '/' FilePath { $$ = realloc($1, sizeof(char) * (strlen($1) + 1 + strlen($3))); strcat($$, "/"); strcat($$, $3); }
  | ID '.' FilePath { $$ = realloc($1, sizeof(char) * (strlen($1) + 1 + strlen($3))); strcat($$, "."); strcat($$, $3); }
  ;

LibraryName:
    ID
  ;

NamespaceItem:
    FuncDeclaration
  | RunDeclaration
  ;

FuncDeclaration:
    ID ':' FunctionType CONSTEQ FuncDefinition { $$ = nalloc(2, FUNCDEC); $$->value.valC = $1; addChild($$, $3); addChild($$, $5); }
  | ID ':' FunctionType '=' FuncDefinition     { $$ = nalloc(2, FUNCDEC); $$->value.valC = $1; addChild($$, $3); addChild($$, $5); }
  | ID ':' FunctionType ';'                    { $$ = nalloc(1, FUNCDEC); $$->value.valC = $1; addChild($$, $3); }
  ;

RunDeclaration:
    "run" CONSTEQ FuncDefinition[func]         { $$ = nalloc(1, RUN); addChild($$, $func); $$->value.valI = 0; }
  | "run" INTEGER CONSTEQ FuncDefinition[func] { $$ = nalloc(1, RUN); addChild($$, $func); $$->value.valI = $2; }
  ;

FuncDefinition:
    Parameters FunctionBody { $$ = nalloc(2, FUNCDEF); addChild($$, $2); addChild($$, $1); }
  | FunctionBody            { $$ = nalloc(1, FUNCDEF); addChild($$, $1); }
  ;

FunctionBody:
    BlockStatement
  ;

Parameters:
    '(' ParameterList ')' { $$ = $2; }
  ;

ParameterList:
    Parameter                   { $$ = nalloc(1, PARAMLIST); addChild($$, $1); }
  | ParameterList ',' Parameter { $$ = $1; addChild($$, $3); }
  ;

Parameter:
    ID          { $$ = nalloc(0, PARAM); $$->value.valC = $1; }
  | ID ':' Type { $$ = nalloc(1, PARAM); $$->value.valC = $1; addChild($$, $3); }
  ;

TypeName:
    QualifiedID
  ;

Type:
    SingleType
  | TupleType
  | FunctionType
  ;

SingleType:
    TypeName { $$ = nalloc(1, TYPESINGLE); addChild($$, $1); }
  ;

TupleType:
    '(' TupleTypes ')' { $$ = $2; }
  ;

TupleTypes:
    Type                { $$ = nalloc(1, TYPEMULTI); addChild($$, $1); }
  | TupleTypes ',' Type { $$ = $1; addChild($$, $3); }
  ;

FunctionType:
    TupleType "->" Type { $$ = nalloc(2, TYPEFN); addChild($$, $1); addChild($$, $3); }
  ;

BlockStatement:
    '{' '}'            { $$ = nalloc(0, BLOCK); }
  | '{' Statements '}' { $$ = $2; }
  ;

Statements:
    Statement            { $$ = nalloc(1, BLOCK); addChild($$, $1); }
  | Statements Statement { $$ = $1; addChild($$, $2); }
  ;

Statement:
    Expression ';'
  ;

QualifiedID:
    QualifiedIDPart                 { $$ = nalloc(1, QUALID); addChild($$, $1); }
  | LibraryName ':' QualifiedIDPart { $$ = nalloc(2, QUALID); addChild($$, nalloc(0, LIBNAME))->value.valC = $1; addChild($$, $3); }
  ;

QualifiedIDPart:
    ID                     { $$ = nalloc(0, QUALPART); $$->value.valC = $1; }
  | ID '.' QualifiedIDPart { $$ = nalloc(0, QUALPART); $$->value.valC = $1; addChild($$, $3); }
  ;

QualifiedIDWithOperators:
    QualifiedID                              { $$ = nalloc(1, QUALIDOP); addChild($$, $1); }
  | PrefixOperator QualifiedIDWithOperators  { $$ = nalloc(2, QUALIDOP); addChild($$, nalloc(0, NONE))->value.valC = $1; addChild($$, $2); }
  | QualifiedIDWithOperators PostfixOperator { $$ = nalloc(2, QUALIDOP); addChild($$, $1); addChild($$, nalloc(0, NONE))->value.valC = $2; }
  ;

Expression:
    AssignExpression
  | BasicExpression
  | DeclExpression
  ;

AssignExpression:
    QualifiedID '=' Expression                { $$ = nalloc(2, EXPRASSIG); addChild($$, $1); addChild($$, $3); }
  | QualifiedID BinaryOperator '=' Expression { $$ = nalloc(2, EXPRASSIG); addChild($$, $1); addChild($$, $4); $$->value.valC = $2; }
  ;

BasicExpression:
    BasicValue                                     { $$ = $1; }
  | '(' Expression ')'                             { $$ = $2; }
  | BasicExpression BinaryOperator BasicExpression { $$ = nalloc(2, EXPRBASIC); addChild($$, $1); $$->value.valC = $2; addChild($$, $3); }
  ;

DeclExpression:
    ID ':' TypeName                { $$ = nalloc(1, EXPRDECL); $$->value.valC = $1; addChild($$, $3); }
  | ID ':' TypeName '=' Expression { $$ = nalloc(2, EXPRDECL); $$->value.valC = $1; addChild($$, $3); addChild($$, $5); }
  | ID ':' '=' Expression          { $$ = nalloc(1, EXPRDECL); $$->value.valC = $1; addChild($$, $4); }
  ;

BasicValue:
    QualifiedIDWithOperators { $$ = $1; }
  | INTEGER                  { $$ = nalloc(0, VALINT); $$->value.valI = $1; }
  | FLOAT                    { $$ = nalloc(0, VALFLOAT); $$->value.valF = $1; }
  | CHAR                     { $$ = nalloc(0, VALINT); $$->value.valI = $1; }
  | STRING                   { $$ = nalloc(0, VALSTR); $$->value.valC = $1; }
  ;

BinaryOperator:
    '+'   { $$ = "+"; }
  | '-'   { $$ = "-"; }
  | '*'   { $$ = "*"; }
  | '/'   { $$ = "/"; }
  | '%'   { $$ = "%"; }
  | '&'   { $$ = "&"; }
  | '|'   { $$ = "|"; }
  | '^'   { $$ = "^"; }
  | "&&"  { $$ = "&&"; }
  | "||"  { $$ = "||"; }
  | "^^"  { $$ = "^^"; }
  | "<<"  { $$ = "<<"; }
  | ">>"  { $$ = ">>"; }
  | ">>>" { $$ = ">>>"; }
  | "=="  { $$ = "=="; }
  | "!="  { $$ = "!="; }
  | '<'   { $$ = "<"; }
  | '>'   { $$ = ">"; }
  | "<="  { $$ = "<="; }
  | ">="  { $$ = ">="; }
  | "<=>" { $$ = "<=>"; }
  ;

PrefixOperator:
    '+'  { $$ = "+"; }
  | '-'  { $$ = "-"; }
  | '*'  { $$ = "*"; }
  | '&'  { $$ = "&"; }
  | "++" { $$ = "++"; }
  | "--" { $$ = "--"; }
  | '!'  { $$ = "!"; }
  | "!!" { $$ = "!!"; }
  ;

PostfixOperator:
    "++" { $$ = "++"; }
  | "--" { $$ = "--"; }
  ;
