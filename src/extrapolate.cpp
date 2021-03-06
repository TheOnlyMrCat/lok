#include "clok.hpp"
#include "program.hpp"
#include "type.hpp"
#include "util.hpp"
#include "grammar.hpp"

#include <boost/filesystem.hpp>
namespace bfs = boost::filesystem;

void Program::locateTypes(std::unique_ptr<Node>& tree) {
	for (auto& node : tree->children) {
		if (node->type == NodeType::TYPEDECL) {
			PLOGD << "Found type declaration";
		} else if (node->type == NodeType::NAMESPACE) {
			PLOGD << "Found namespace statement";
			context.currentNamespace = Identifier(node->children[0], true, context).parts;
			locateTypes(node->children[1]);
			context.currentNamespace.clear();
		}
	}
}

void Program::findSymbols(std::unique_ptr<Node>& tree) {
	for (auto& node : tree->children) {
		if (node->type == NodeType::DECL) {
			PLOGD << "Found declaration";
			Symbol s = Symbol(node, false, context);
			context.symbols.emplace(std::make_pair(s.id, std::move(s)));
		} else if (node->type == NodeType::TYPEDECL) {
			PLOGD << "Found type declaration";
			context.currentNamespace.push_back(strings[node->value.valC]);
			findSymbols(node->children[0]->children[0]);
			context.currentNamespace.pop_back();
			Symbol s = Symbol(node, true, context);
			context.symbols.emplace(std::make_pair(s.id, std::move(s)));
		} else if (node->type == NodeType::CTORDEF) {
			PLOGD << "Found constructor definition";
			auto ctorname = std::vector<IdPart>(context.currentNamespace);
			ctorname.emplace_back("new", false);
			std::vector<Type> params;
			for (auto& param : node->children[0]->children) {
				params.emplace_back(param->children[0], context);
			}
			Symbol s = Symbol(ReturningType(TupleType(params), Type(SingleType(Identifier(context.currentNamespace)))), ctorname);
			if (node->children[node->children.size() - 1]->type == NodeType::ATTRS) {
				s.fillAttributes(node->children[node->children.size() - 1]);
			}
			if (s.attr.attrs.find("forced") != s.attr.attrs.end()) {
				PLOGD << "(Constructor is forced constructor)";
				if (params.size() > 1) {
					PLOGE << "Forced constructor with more than one parameter"; //* Error location
					throw; //? Convert to forced tuple upgrade?
				}
				context.forcedConstructors.emplace(std::make_pair(params[0], s));
			}
			context.symbols.emplace(std::make_pair(s.id, s));
		} else if (node->type == NodeType::DTORDEF) {
			PLOGD << "Found destructor definition";
			auto dtorname = std::vector<IdPart>(context.currentNamespace);
			dtorname.emplace_back("del", false);
			Symbol s = Symbol(Type(SingleType(Identifier(context.currentNamespace))), dtorname);
			if (node->children[node->children.size() - 1]->type == NodeType::ATTRS) {
				s.fillAttributes(node->children[node->children.size() - 1]);
			}
			context.symbols.emplace(std::make_pair(s.id, std::move(s)));
		} else if (node->type == NodeType::OPOVERLOAD) {
			PLOGD << "Found operator overload definition";
			auto overloadname = std::vector<IdPart>(context.currentNamespace);
			overloadname.emplace_back(strings[node->value.valC], false);
			Symbol s = Symbol(typeFromFunction(node->children[0], context), overloadname);
			if (node->children[node->children.size() - 1]->type == NodeType::ATTRS) {
				s.fillAttributes(node->children[node->children.size() - 1]);
			}
			context.symbols.emplace(std::make_pair(s.id, s));
			context.opOverloads.emplace(strings[node->value.valC], s);
		} else if (node->type == NodeType::NAMESPACE) {
			PLOGD << "Found namespace statement";
			context.currentNamespace = Identifier(node->children[0], true, context).parts;
			findSymbols(node->children[1]);
			context.currentNamespace.clear();
		}
	}
}

Type checkForOverload(SingleType l, SingleType r, std::string op, ProgramContext &pc) {
	std::vector<IdPart> symbolLoc = l.id.parts;
	symbolLoc.emplace_back(op, false);
	auto s = pc.symbols.find(symbolLoc);
	return s == pc.symbols.end() ? Type() : s->second.type;
}

Type checkTuple(TupleType l, TupleType r, std::string op, ProgramContext &pc) {
	bool valid = l.types.size() != r.types.size();
	std::vector<Type> types;
	for (int i = 0; valid && i < l.types.size(); i++) {
		Type lType = l.types[i], rType = r.types[i];
		if (lType.typeType == 2 || rType.typeType == 2) {
			valid = false;
		} else if (lType.typeType == 1) {
			if (rType.typeType != 1) {
				valid = false;
			} else {
				Type t = checkTuple(*lType.tuple, *rType.tuple, op, pc);
				valid = t.typeType != -1;
				types.push_back(t);
			}
		} else {
			if (rType.typeType != 0) {
				valid = false;
			} else {
				Type t = checkForOverload(*lType.basic, *rType.basic, op, pc);
				valid = t.typeType != -1;
				types.push_back(t);
			}
		}
	}
	return valid ? Type(types) : Type();
}

Expr *forceUpgrade(Expr *expr, Type finalType, ProgramContext &pc) {
	Type type = expr->type;
	auto iter = pc.forcedConstructors.equal_range(type);
	std::vector<Symbol> possibleConstructors;
	for (auto i = iter.first; i != iter.second; i++) {
		possibleConstructors.push_back(i->second);
	}
	if (possibleConstructors.size() > 0) {
		auto found = std::find_if(possibleConstructors.begin(), possibleConstructors.end(), [finalType](const Symbol& s) { return s.type.func->output == finalType; });
		if (found != possibleConstructors.end()) {
			return new CallExpr(new SymbolExpr(*found), ArgsExpr({expr}));
		} else if (possibleConstructors.size() == 1) {
			return new CallExpr(new SymbolExpr(possibleConstructors[0]), ArgsExpr({expr}));
		} else {
			PLOGW << "Multiple forced constructors can operate on expression"; //* Warning location
		}
	}
	return expr;
} 

Expr *coerceType(Expr *expr, Type finalType, ProgramContext &pc) {
	Type type = expr->type;
	if (type == finalType) return expr;
	// Forced upgrades
	
	return nullptr;
}

bool functionMatches(ReturningType r, ArgsExpr args) {
	if (r.input.types.size() != args.type.tuple->types.size()) return false;
	for (int i = 0; i < r.input.types.size(); i++) {
		if (!(r.input.types[i] == args.type.tuple->types[i])) return false;
	}
	return true;
}

Expr *getOverload(Expr *lt, Expr *rt, std::string op, ProgramContext &pc) {
	auto its = pc.opOverloads.equal_range(op);
	if (its.first == pc.opOverloads.end()) {
		PLOGE << "Somehow that operator hasn't been overloaded at all"; //* Error location
		throw;
	}
	std::vector<Expr*> possible;
	for (auto i = its.first; i != its.second; i++) {
		if (i->first != op) continue;
		Type t1 = i->second.type.func->input.types[0];
		Type t2 = i->second.type.func->input.types[1];
		if (t1 == lt->type && t2 == rt->type) return new CallExpr(new SymbolExpr(i->second), ArgsExpr({lt, rt}));
		Expr *clt = coerceType(lt, t1, pc);
		Expr *crt = coerceType(rt, t2, pc);
		if (clt && crt) possible.push_back(new CallExpr(new SymbolExpr(i->second), ArgsExpr({lt, rt})));
	}
	if (possible.size() > 1) {
		PLOGE << "Cannot determine type to match overload"; //* Error location
		throw;
	} else if (possible.size() == 0) {
		PLOGE << "No overloads match the input arguments"; //* Error location
		throw;
	} else {
		PLOGW << "Implicit type conversion in operator expression"; //* Warning location
		return possible[0];
	}
}

Type functionReturns(Type t) {
	if (t.typeType == 1) {
		std::vector<Type> types;
		for (auto tx : t.tuple->types) {
			Type r = functionReturns(tx);
			if (r.typeType == -1) return Type();
			types.push_back(r);
		}
		return Type(types);
	} else if (t.typeType == 2) {
		return t.func->output;
	} else {
		return Type();
	}
}

Statement *Program::_extrapStmt(std::unique_ptr<Node>& node) {
	Statement *s;
	switch (node->type) {
		case NodeType::DECL: {
				PLOGD << "A variable declaration";
				Identifier id = Identifier({{strings[node->value.valC], false}});
				Type type;
				int expressionIndex = 0;
				if (node->children[0]->type == NodeType::TYPESINGLE || node->children[0]->type == NodeType::TYPEMULTI || node->children[0]->type == NodeType::TYPEFN) {
					type = Type(node->children[0], context);
					expressionIndex = 1;
				}
				Expr *expr = nullptr;
				if (node->children.size() > expressionIndex && node->children[expressionIndex]->type != NodeType::ATTRS) {
					expr = forceUpgrade(_extrapolate(node->children[expressionIndex]), type, context);
					if (type.typeType != -1 && !(expr->type == type)) {
					PLOGE << "No bad type doesn't match"; //* Error location
					throw;
				} else {
					type = expr->type;
				}
			}
			context.stackFrames.back().symbols.emplace_back(type, id);
			return new DeclStmt(id, type, nullptr);
		}
		case NodeType::BLOCK: {
			std::vector<Statement*> statements;
			context.stackFrames.emplace_back();
			for (auto& n : node->children) {
				statements.push_back(_extrapStmt(n));
			}
			context.stackFrames.pop_back();
			return new BlockStmt(statements);
		}
		case NodeType::STMTDOWHILE:
		case NodeType::STMTWHILE:
			PLOGD << "A while statement, containing...";
			s = new WhileStmt(_extrapolate(node->children[0]), _extrapStmt(node->children[0]), node->type == NodeType::STMTDOWHILE);
			PLOGD << "... (while) nothing else";
			return s;
		case NodeType::STMTDOFOR:
			PLOGD << "A for statement, containing...";
			context.stackFrames.emplace_back();
			s = new ForStmt(node->children.size() > 2 ? _extrapStmt(node->children[2]) : nullptr, _extrapolate(node->children[0]->children[0]), _extrapolate(node->children[0]->children[1]), _extrapStmt(node->children[1]), true);
			context.stackFrames.pop_back();
			PLOGD << "... (for) nothing else";
			return s;
		case NodeType::STMTFOR:
			PLOGD << "A for statement, containing...";
			context.stackFrames.emplace_back();
			s = new ForStmt(_extrapStmt(node->children[0]->children[0]), _extrapolate(node->children[0]->children[1]), _extrapolate(node->children[0]->children[2]), _extrapStmt(node->children[1]), false);
			context.stackFrames.pop_back();
			PLOGD << "... (for) nothing else";
			return s;
		case NodeType::STMTRTN:
			PLOGD << "A return statement:";
			return new ReturnStmt(_extrapolate(node->children[0]));
		case NodeType::STMTIF:
			PLOGD << "An if statement, containing...";
			s = new IfStmt(_extrapolate(node->children[0]), _extrapStmt(node->children[1]), node->children.size() > 2 ? _extrapStmt(node->children[2]) : nullptr);
			PLOGD << "... (if) nothing else";
			return s;
		case NodeType::EXPRBASIC:
		case NodeType::FUNCDEF:
		case NodeType::VALSTR:
		case NodeType::VALINT:
		case NodeType::VALFLOAT:
		case NodeType::VALBIT:
		case NodeType::FQUALPATH:
			return _extrapolate(node);
		case NodeType::NONE:
			return nullptr;
		default:
			PLOGF << "Unhandled statement type";
			throw;
	}
}

Expr *Program::_extrapolate(std::unique_ptr<Node>& node) {
	switch (node->type) {
		case NodeType::FUNCDEF: {
			PLOGD << "A function definition, containing...";
			ReturningType type = typeFromFunction(node, context);
			Type returnedType;
			StackFrame frame;
			for (auto& param : node->children[1]->children) {
				frame.symbols.emplace_back(Type(param->children[0], context), Identifier({{strings[param->value.valC], false}}));
			}
			context.stackFrames.emplace_back(frame);
			Statement *s = _extrapStmt(node->children[0]);
			context.stackFrames.pop_back();
			PLOGD << "... (func) nothing else";
			return new FuncValue(type, s);
		}
		case NodeType::EXPRBASIC: {
			PLOGD << "A basic expression, containing...";
			Expr *left = _extrapolate(node->children[0]);
			Expr *fLeft = forceUpgrade(left, Type(), context);
			if (node->children.size() > 1) {
				Expr *right = _extrapolate(node->children[1]);
				Type lType = fLeft->type;
				Type rType = right->type;
				Type exprType;
				std::string op = strings[node->value.valC];
				PLOGD << "... (expr) with operator " << op;
				if (lType.typeType == 2 && op == "()") {
					std::vector<Type> params = lType.func->input.types;
					ArgsExpr *args = static_cast<ArgsExpr*>(right);
					std::vector<Expr*> exprs;
					for (int i = 0; i < params.size(); i++) {
						Expr *arg = coerceType(args->expressions[i], params[i], context);
						if (!arg) {
							PLOGE << "Could not convert argument to required type"; //* Error location
							throw;
						}
						exprs.push_back(arg);
					}
					return new CallExpr(fLeft, ArgsExpr(exprs));
				} else if (lType.typeType == 1) {
					Expr *fRight = forceUpgrade(right, Type(), context);
					return getOverload(fLeft, fRight, op, context);
				} else if (lType.typeType == 0) {
					Expr *fRight = forceUpgrade(right, Type(), context);
					return getOverload(fLeft, fRight, op, context);
				} else PLOGF << "AAAAAAAAAA"; throw; // I don't know what would have happened here but it's not pretty
			} else {
				std::string op = strings[node->value.valC];
				PLOGD << "... (expr) with " << (node->value.valB ? "postfix" : "prefix") << " operator " << op;
				//TODO return something and check for overloads
				return nullptr;
			}
		}
		case NodeType::ARGLIST: {
			PLOGD << "An argument list, containing...";
			std::vector<Expr*> exprs;
			for (auto& child : node->children) {
				exprs.push_back(_extrapolate(child));
			}
			PLOGD << "... (args) nothing else";
			return new ArgsExpr(exprs);
		}
		case NodeType::VALSTR:
			PLOGD << "A string literal";
			return new StringValue(strings[node->value.valC]);
		case NodeType::VALINT:
			PLOGD << "An integer literal";
			return new IntValue(node->value.valI, 64);
		case NodeType::VALFLOAT:
			PLOGD << "A float literal";
			return new FloatValue(node->value.valF, 64);
		case NodeType::VALBIT:
			PLOGD << "A bit literal";
			return new BitValue(node->value.valB);
		case NodeType::FQUALPATH:
			PLOGD << "A symbol reference to...";
			return new SymbolExpr(Identifier(node, true, context), context);
		case NodeType::NONE:
			return nullptr;
		default:
			PLOGF << "Unhandled expression type";
			throw; //TODO see above
	}
}

void Program::extrapolate(std::unique_ptr<Node>& tree, std::unordered_map<std::string, Program>& programs, std::string workingDir) {
	for (auto& node : tree->children) {
		if (node->type == NodeType::DECL) {
			Type expectedType;
			int expressionIndex = 0;
			if (node->children[0]->type == NodeType::TYPESINGLE || node->children[0]->type == NodeType::TYPEMULTI || node->children[0]->type == NodeType::TYPEFN) {
				expectedType = Type(node->children[0], context);
				expressionIndex = 1;
			}
			if (node->children.size() < expressionIndex) {
				Expr *extrapolated = _extrapolate(node->children[expressionIndex]);
				if (expectedType.typeType != -1 && !(extrapolated->type == expectedType)) {
					//TODO check for conversion
					PLOGE << "No bad type doesn't match";
					throw; //TODO see above
				}
				Symbol *s = &context.symbols.find(Symbol(node, false, context).id)->second;
				s->type = expectedType;
				extrapolatedSymbols.push_back({s, extrapolated});
			}
		} else if (node->type == NodeType::NAMESPACE) {
			context.currentNamespace = Identifier(node->children[0], true, context).parts;
			extrapolate(node->children[1], programs, workingDir);
			context.currentNamespace.clear();
		} else if (node->type == NodeType::RUN) {
			extrapolatedSymbols.push_back({new Symbol(typeFromFunction(node->children[0], context), Identifier({{"run", false}, {"0", false}})), _extrapolate(node->children[0]), true});
		} else if (node->type == NodeType::TYPEDECL) {
			auto oldNamespace = std::vector<IdPart>(context.currentNamespace);
			context.currentNamespace.push_back({strings[node->value.valC], true});
			extrapolate(node->children[0]->children[0], programs, workingDir);
			context.currentNamespace = oldNamespace;
		} else if (node->type == NodeType::OPOVERLOAD) {
			auto overloadname = std::vector<IdPart>(context.currentNamespace);
			overloadname.emplace_back(strings[node->value.valC], false);
			Symbol *s = &context.symbols.find(Identifier(overloadname))->second;
			extrapolatedSymbols.push_back({s, _extrapolate(node->children[0])});
		} else if (node->type == NodeType::LOAD) {
			std::string filename = bfs::canonical(parseFilename(node->children[0]->children[0]) + ".lok", bfs::path(workingDir)).c_str();
			context.externalSymbols[filename] = programs[filename].context.symbols;
			PLOGD << "A load statement, referencing " << filename
				  << " (" << context.externalSymbols[filename].size() << " symbols loaded)";
		} else if (node->type == NodeType::CTORDEF) {
			PLOGD << "A constructor definition, containing...";
			auto ctorname = std::vector<IdPart>(context.currentNamespace);
			ctorname.emplace_back("new", false);
			Symbol *s = &context.symbols.find(Identifier(ctorname))->second;
			//TODO CtorInit node type
			if (node->children[1]->type == NodeType::BLOCK) {
				extrapolatedSymbols.push_back({s, _extrapStmt(node->children[1])});
			} else if (node->children.size() >= 3 && node->children[2]->type == NodeType::BLOCK) {
				extrapolatedSymbols.push_back({s, _extrapStmt(node->children[2])});
			}
		} else if (node->type == NodeType::DTORDEF) {
			PLOGD << "A destructor definition, containing...";
			auto dtorname = std::vector<IdPart>(context.currentNamespace);
			dtorname.emplace_back("del", false);
			Symbol *s = &context.symbols.find(Identifier(dtorname))->second;
			extrapolatedSymbols.push_back({s, _extrapStmt(node->children[0])});
		}
	}
}