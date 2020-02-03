#include "type.hpp"

#include "clok.hpp"
#include "program.hpp"
#include "util.hpp"

Type::Type() {
	typeType = -1;
}

Type::Type(NodePtr& node, ProgramContext& pc) {
	switch (node->type) {
		case NodeType::TYPESINGLE:
			typeType = 0;
			basic = val::value_ptr<SingleType>(SingleType(node, pc));
			break;
		case NodeType::TYPEMULTI:
			typeType = 1;
			tuple = val::value_ptr<TupleType>(TupleType(node, pc));
			break;
		case NodeType::TYPEFN:
			typeType = 2;
			func = val::value_ptr<ReturningType>(ReturningType(node, pc));
			break;
		default:
			PLOGF << "I sincerely hope that this never happens";
			exit(1);
	}
}

Type::~Type() = default;

Type::Type(const Type&) = default;
Type::Type(Type&&) = default;
Type& Type::operator=(Type&&) = default;

// Node is expected to be of type QUALID
Identifier::Identifier(NodePtr& node, ProgramContext& context) : parts(context.currentNamespace) {
	NodePtr *part;
    for (part = &node->children[0]; (*part)->children.size() > 0; part = &(*part)->children[0]) {
        parts.push_back({strings[(*part)->value.valC], false}); //TODO: Resolve types
    }
	parts.push_back({strings[(*part)->value.valC], false});
}

Identifier::Identifier(std::vector<IdPart> parts): parts(parts) {}

TypeQualifier::TypeQualifier(NodePtr& node) {
	isPointer = node->type == NodeType::TYPEQUALPTR;
	if (node->type == NodeType::TYPEQUALARR) arraySize = node->value.valI;
	if (node->children[0]->type != NodeType::NONE) nested = val::value_ptr<TypeQualifier>(node->children[0]);
}

SingleType::SingleType(NodePtr& node, ProgramContext& pc):
	id(node->children[0], pc),
	qualifier(node->children[1]->type != NodeType::NONE ? val::value_ptr<TypeQualifier>(node->children[1]) : val::value_ptr<TypeQualifier>(nullptr))
{}

TupleType::TupleType(NodePtr& node, ProgramContext& pc) {
    for (int i = 0; i < node->children.size(); i++) {
        types.push_back(Type(node->children[i], pc));
    }
}

ReturningType::ReturningType(NodePtr& node, ProgramContext& pc): input(node->children[0], pc), output(node->children[1], pc) {}

Symbol::Symbol(NodePtr& node, bool isType, ProgramContext& pc): id(combineParts(pc.currentNamespace, {strings[node->value.valC], isType})) {
	if (node->children[0]->type == NodeType::TYPESINGLE || node->children[0]->type == NodeType::TYPEMULTI || node->children[0]->type == NodeType::TYPEFN) {
		type = Type(node->children[0], pc);
	}
}

Expr::Expr(Type t): type(t) {}
Expr::~Expr() = default;

OpExpr::OpExpr(Type t, Expr *l, Expr *r, std::string o): Expr(t), left(std::move(l)), right(std::move(r)), op(o) {}
OpExpr::~OpExpr() {
	delete left;
	delete right;
}

std::string Symbol::toLokConv() {
	//TODO
	std::string sb;
	for (auto i : id.parts) {
		sb += '_';
		sb += i.first;
	}
	return sb;
}