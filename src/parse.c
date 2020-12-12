#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "identifier.h"
#include "lex.h"
#include "parse.h"
#include "trace.h"
#include "types.h"
#include "utf8.h"

struct parser {
	struct lexer *lex;
};

static void
synassert_msg(bool cond, const char *msg, struct token *tok)
{
	if (!cond) {
		fprintf(stderr, "Syntax error: %s at %s:%d:%d ('%s')\n", msg,
			tok->loc.path, tok->loc.lineno, tok->loc.colno,
			token_str(tok));
		exit(1);
	}
}

static void
synassert(bool cond, struct token *tok, ...)
{
	if (!cond) {
		va_list ap;
		va_start(ap, tok);

		enum lexical_token t = va_arg(ap, enum lexical_token);
		fprintf(stderr,
			"Syntax error: unexpected '%s' at %s:%d:%d%s",
			token_str(tok), tok->loc.path, tok->loc.lineno,
			tok->loc.colno, t == T_EOF ? "\n" : ", expected " );
		while (t != T_EOF) {
			if (t == T_LITERAL || t == T_NAME) {
				fprintf(stderr, "%s", lexical_token_str(t));
			} else {
				fprintf(stderr, "'%s'", lexical_token_str(t));
			}
			t = va_arg(ap, enum lexical_token);
			fprintf(stderr, "%s", t == T_EOF ? "\n" : ", ");
		}
		exit(1);
	}
}

static void
want(struct parser *par, enum lexical_token ltok, struct token *tok)
{
	struct token _tok = {0};
	struct token *out = tok ? tok : &_tok;
	lex(par->lex, out);
	synassert(out->token == ltok, out, ltok, T_EOF);
	if (!tok) {
		token_finish(out);
	}
}

static void
parse_identifier(struct parser *par, struct identifier *ident)
{
	struct token tok = {0};
	struct identifier *i = ident;
	trenter(TR_PARSE, "identifier");

	while (!i->name) {
		want(par, T_NAME, &tok);
		i->name = strdup(tok.name);
		token_finish(&tok);

		struct identifier *ns;
		switch (lex(par->lex, &tok)) {
		case T_DOUBLE_COLON:
			ns = calloc(1, sizeof(struct identifier));
			*ns = *i;
			i->ns = ns;
			i->name = NULL;
			break;
		default:
			unlex(par->lex, &tok);
			break;
		}
	}

	char buf[1024];
	identifier_unparse_static(ident, buf, sizeof(buf));
	trleave(TR_PARSE, "%s", buf);
}

static void
parse_import(struct parser *par, struct ast_imports *imports)
{
	trenter(TR_PARSE, "import");
	struct identifier ident = {0};
	parse_identifier(par, &ident);

	struct token tok = {0};
	switch (lex(par->lex, &tok)) {
	case T_EQUAL:
		assert(0); // TODO
	case T_LBRACE:
		assert(0); // TODO
	case T_SEMICOLON:
		imports->mode = AST_IMPORT_IDENTIFIER;
		imports->ident = ident;
		break;
	default:
		synassert(false, &tok, T_EQUAL, T_LBRACE, T_SEMICOLON, T_EOF);
		break;
	}

	trleave(TR_PARSE, NULL);
}

static void
parse_imports(struct parser *par, struct ast_subunit *subunit)
{
	trenter(TR_PARSE, "imports");
	struct token tok = {0};
	struct ast_imports **next = &subunit->imports;

	bool more = true;
	while (more) {
		struct ast_imports *imports;
		switch (lex(par->lex, &tok)) {
		case T_USE:
			imports = calloc(1, sizeof(struct ast_imports));
			parse_import(par, imports);
			*next = imports;
			next = &imports->next;
			break;
		default:
			unlex(par->lex, &tok);
			more = false;
			break;
		}
	}

	for (struct ast_imports *i = subunit->imports; i; i = i->next) {
		char buf[1024];
		identifier_unparse_static(&i->ident, buf, sizeof(buf));
		trace(TR_PARSE, "use %s", buf);
	}
	trleave(TR_PARSE, NULL);
}

static void parse_type(struct parser *par, struct ast_type *type);

static void
parse_parameter_list(struct parser *par, struct ast_function_type *type)
{
	trenter(TR_PARSE, "parameter-list");
	struct token tok = {0};
	bool more = true;
	struct ast_function_parameters **next = &type->parameters;
	while (more) {
		*next = calloc(1, sizeof(struct ast_function_parameters));
		(*next)->type = calloc(1, sizeof(struct ast_type));
		want(par, T_NAME, &tok);
		(*next)->name = tok.name;
		want(par, T_COLON, NULL);
		parse_type(par, (*next)->type);
		trace(TR_PARSE, "%s: [type]", (*next)->name);
		switch (lex(par->lex, &tok)) {
		case T_COMMA:
			switch (lex(par->lex, &tok)) {
			case T_ELLIPSIS:
				type->variadism = VARIADISM_HARE;
				if (lex(par->lex, &tok) != T_COMMA) {
					unlex(par->lex, &tok);
				}
				more = false;
				trace(TR_PARSE, ", ...");
				break;
			default:
				unlex(par->lex, &tok);
				next = &(*next)->next;
				break;
			}
			break;
		case T_ELLIPSIS:
			type->variadism = VARIADISM_C;
			if (lex(par->lex, &tok) != T_COMMA) {
				unlex(par->lex, &tok);
			}
			more = false;
			trace(TR_PARSE, "...");
			break;
		default:
			more = false;
			unlex(par->lex, &tok);
			break;
		}
	}
	trleave(TR_PARSE, NULL);
}

static void
parse_prototype(struct parser *par, struct ast_function_type *type)
{
	trenter(TR_PARSE, "prototype");
	want(par, T_LPAREN, NULL);
	struct token tok = {0};
	if (lex(par->lex, &tok) != T_RPAREN) {
		unlex(par->lex, &tok);
		parse_parameter_list(par, type);
		want(par, T_RPAREN, NULL);
	}
	type->result = calloc(1, sizeof(struct ast_type));
	parse_type(par, type->result);
	size_t ctr = 0;
	for (struct ast_function_parameters *param = type->parameters; param;
			param = param->next) {
		ctr++;
	}
	trace(TR_PARSE, "[%zu parameters] [type]", ctr);
	trleave(TR_PARSE, NULL);
}

static void
parse_type(struct parser *par, struct ast_type *type)
{
	trenter(TR_PARSE, "type");
	struct token tok = {0};
	switch (lex(par->lex, &tok)) {
	case T_CONST:
		type->constant = true;
		break;
	default:
		unlex(par->lex, &tok);
		break;
	}
	switch (lex(par->lex, &tok)) {
	case T_I8:
		type->storage = TYPE_STORAGE_I8;
		break;
	case T_I16:
		type->storage = TYPE_STORAGE_I16;
		break;
	case T_I32:
		type->storage = TYPE_STORAGE_I32;
		break;
	case T_I64:
		type->storage = TYPE_STORAGE_I64;
		break;
	case T_U8:
		type->storage = TYPE_STORAGE_U8;
		break;
	case T_U16:
		type->storage = TYPE_STORAGE_U16;
		break;
	case T_U32:
		type->storage = TYPE_STORAGE_U32;
		break;
	case T_U64:
		type->storage = TYPE_STORAGE_U64;
		break;
	case T_INT:
		type->storage = TYPE_STORAGE_INT;
		break;
	case T_UINT:
		type->storage = TYPE_STORAGE_UINT;
		break;
	case T_SIZE:
		type->storage = TYPE_STORAGE_SIZE;
		break;
	case T_UINTPTR:
		type->storage = TYPE_STORAGE_UINTPTR;
		break;
	case T_CHAR:
		type->storage = TYPE_STORAGE_CHAR;
		break;
	case T_RUNE:
		type->storage = TYPE_STORAGE_RUNE;
		break;
	case T_STR:
		type->storage = TYPE_STORAGE_STRING;
		break;
	case T_F32:
		type->storage = TYPE_STORAGE_F32;
		break;
	case T_F64:
		type->storage = TYPE_STORAGE_F64;
		break;
	case T_BOOL:
		type->storage = TYPE_STORAGE_BOOL;
		break;
	case T_VOID:
		type->storage = TYPE_STORAGE_VOID;
		break;
	case T_ENUM:
		assert(0); // TODO: Enums
	case T_NULLABLE:
		type->pointer.nullable = true;
		want(par, T_TIMES, NULL);
		trace(TR_PARSE, "nullable");
		/* fallthrough */
	case T_TIMES:
		type->storage = TYPE_STORAGE_POINTER;
		type->pointer.referent = calloc(1, sizeof(struct ast_type));
		parse_type(par, type->pointer.referent);
		break;
	case T_STRUCT:
	case T_UNION:
		assert(0); // TODO: Structs/unions
	case T_LPAREN:
		assert(0); // TODO: Tagged unions
	case T_LBRACKET:
		assert(0); // TODO: Slices/arrays
	case T_ATTR_NORETURN:
		type->function.noreturn = true;
		want(par, T_FN, NULL);
		/* fallthrough */
	case T_FN:
		type->storage = TYPE_STORAGE_FUNCTION;
		parse_prototype(par, &type->function);
		break;
	default:
		unlex(par->lex, &tok);
		type->storage = TYPE_STORAGE_ALIAS;
		parse_identifier(par, &type->alias);
		break;
	}
	trleave(TR_PARSE, "%s%s", type->constant ? "const " : "",
		type_storage_unparse(type->storage));
}

static void
parse_simple_expression(struct parser *par, struct ast_expression *exp)
{
	trenter(TR_PARSE, "simple-expression");
	struct token tok = {0};
	lex(par->lex, &tok);
	assert(tok.token == T_LITERAL); // TODO: other simple expressions
	trenter(TR_PARSE, "constant");
	exp->type = EXPR_CONSTANT;
	exp->constant.storage = tok.storage;
	switch (tok.storage) {
	case TYPE_STORAGE_CHAR:
	case TYPE_STORAGE_U8:
	case TYPE_STORAGE_U16:
	case TYPE_STORAGE_U32:
	case TYPE_STORAGE_U64:
	case TYPE_STORAGE_UINT:
	case TYPE_STORAGE_UINTPTR:
	case TYPE_STORAGE_SIZE:
		exp->constant._unsigned = (uintmax_t)tok._unsigned;
		break;
	case TYPE_STORAGE_I8:
	case TYPE_STORAGE_I16:
	case TYPE_STORAGE_I32:
	case TYPE_STORAGE_I64:
	case TYPE_STORAGE_INT:
		exp->constant._signed = (intmax_t)tok._signed;
		break;
	case TYPE_STORAGE_STRING:
		exp->constant.string.len = tok.string.len;
		exp->constant.string.value = tok.string.value;
		break;
	default:
		assert(0); // TODO
	}
	trleave(TR_PARSE, "%s", token_str(&tok));
	trleave(TR_PARSE, NULL);
}

static void
parse_complex_expression(struct parser *par, struct ast_expression *exp)
{
	// TODO: other complex expressions
	trenter(TR_PARSE, "complex-expression");
	parse_simple_expression(par, exp);
	trleave(TR_PARSE, NULL);
}

static char *
parse_attr_symbol(struct parser *par)
{
	struct token tok = {0};
	want(par, T_LPAREN, NULL);
	want(par, T_LITERAL, &tok);
	synassert_msg(tok.storage == TYPE_STORAGE_STRING,
		"expected string literal", &tok);
	for (size_t i = 0; i < tok.string.len; i++) {
		uint32_t c = tok.string.value[i];
		synassert_msg(c <= 0x7F && (isalnum(c) || c == '_' || c == '$'
			|| c == '.'), "invalid symbol", &tok);
		synassert_msg(i != 0 || (!isdigit(c) && c != '$'),
			"invalid symbol", &tok);
	}
	want(par, T_RPAREN, NULL);
	return tok.string.value;
}

static void
parse_global_decl(struct parser *par, enum lexical_token mode,
		struct ast_global_decl *decl)
{
	trenter(TR_PARSE, "global");
	struct token tok = {0};
	struct ast_global_decl *i = decl;
	assert(mode == T_LET || mode == T_CONST || mode == T_DEF);
	bool more = true;
	while (more) {
		if (mode == T_LET || mode == T_CONST) {
			switch (lex(par->lex, &tok)) {
			case T_ATTR_SYMBOL:
				i->symbol = parse_attr_symbol(par);
				break;
			default:
				unlex(par->lex, &tok);
				break;
			}
		}
		parse_identifier(par, &i->ident);
		want(par, T_COLON, NULL);
		parse_type(par, &i->type);
		if (mode == T_CONST) {
			i->type.constant = true;
		}
		want(par, T_EQUAL, NULL);
		parse_simple_expression(par, &i->init);
		switch (lex(par->lex, &tok)) {
		case T_COMMA:
			lex(par->lex, &tok);
			if (tok.token == T_NAME || tok.token == T_ATTR_SYMBOL) {
				i->next = calloc(1, sizeof(struct ast_global_decl));
				i = i->next;
				unlex(par->lex, &tok);
				break;
			}
			/* fallthrough */
		default:
			more = false;
			unlex(par->lex, &tok);
			break;
		}
	}

	for (struct ast_global_decl *i = decl; i; i = i->next) {
		char buf[1024];
		identifier_unparse_static(&i->ident, buf, sizeof(buf));
		if (decl->symbol) {
			trace(TR_PARSE, "%s @symbol(\"%s\") %s: [type] = [expr]",
				lexical_token_str(mode), decl->symbol, buf);
		} else {
			trace(TR_PARSE, "%s %s: [type] = [expr]",
				lexical_token_str(mode), buf);
		}
	}
	trleave(TR_PARSE, NULL);
}

static void
parse_type_decl(struct parser *par, struct ast_type_decl *decl)
{
	trenter(TR_PARSE, "typedef");
	struct token tok = {0};
	struct ast_type_decl *i = decl;
	bool more = true;
	while (more) {
		parse_identifier(par, &i->ident);
		want(par, T_EQUAL, NULL);
		parse_type(par, &i->type);
		switch (lex(par->lex, &tok)) {
		case T_COMMA:
			lex(par->lex, &tok);
			if (lex(par->lex, &tok) == T_NAME) {
				i->next = calloc(1, sizeof(struct ast_type_decl));
				i = i->next;
				unlex(par->lex, &tok);
				break;
			}
			/* fallthrough */
		default:
			more = false;
			unlex(par->lex, &tok);
			break;
		}
	}

	for (struct ast_type_decl *i = decl; i; i = i->next) {
		char ibuf[1024], tbuf[1024];
		identifier_unparse_static(&i->ident, ibuf, sizeof(ibuf));
		strncpy(tbuf, "[type]", sizeof(tbuf)); // TODO: unparse type
		trace(TR_PARSE, "def %s = %s", ibuf, tbuf);
	}
	trleave(TR_PARSE, NULL);
}

static void
parse_fn_decl(struct parser *par, struct ast_function_decl *decl)
{
	trenter(TR_PARSE, "fn");
	struct token tok = {0};
	bool more = true;
	while (more) {
		switch (lex(par->lex, &tok)) {
		case T_ATTR_FINI:
			decl->flags |= FN_FINI;
			break;
		case T_ATTR_INIT:
			decl->flags |= FN_INIT;
			break;
		case T_ATTR_SYMBOL:
			decl->symbol = parse_attr_symbol(par);
			break;
		case T_ATTR_TEST:
			decl->flags |= FN_TEST;
			break;
		case T_ATTR_NORETURN:
			decl->prototype.noreturn = true;
			break;
		default:
			more = false;
			unlex(par->lex, &tok);
			break;
		}
	}
	want(par, T_FN, NULL);
	parse_identifier(par, &decl->ident);
	parse_prototype(par, &decl->prototype);
	want(par, T_EQUAL, NULL);
	parse_complex_expression(par, &decl->body);

	char symbol[1024], buf[1024];
	if (decl->symbol) {
		snprintf(symbol, sizeof(symbol), "@symbol(\"%s\") ", decl->symbol);
	}
	identifier_unparse_static(&decl->ident, buf, sizeof(buf));
	trace(TR_PARSE, "%s%s%s%s%sfn %s [prototype] = [expr]",
		decl->flags & FN_FINI ? "@fini " : "",
		decl->flags & FN_INIT ? "@init " : "",
		decl->prototype.noreturn ? "@noreturn " : "",
		decl->flags & FN_TEST ? "@test " : "",
		decl->symbol ? symbol : "", buf);
	trleave(TR_PARSE, NULL);
}

static void
parse_decl(struct parser *par, struct ast_decl *decl)
{
	struct token tok = {0};
	switch (lex(par->lex, &tok)) {
	case T_CONST:
	case T_LET:
		decl->decl_type = AST_DECL_GLOBAL;
		parse_global_decl(par, tok.token, &decl->global);
		break;
	case T_DEF:
		decl->decl_type = AST_DECL_CONST;
		parse_global_decl(par, tok.token, &decl->constant);
		break;
	case T_TYPE:
		decl->decl_type = AST_DECL_TYPE;
		parse_type_decl(par, &decl->type);
		break;
	default:
		unlex(par->lex, &tok);
		decl->decl_type = AST_DECL_FUNC;
		parse_fn_decl(par, &decl->function);
		break;
	}
}

static void
parse_decls(struct parser *par, struct ast_decls *decls)
{
	trenter(TR_PARSE, "decls");
	struct token tok = {0};
	struct ast_decls **next = &decls;
	while (tok.token != T_EOF) {
		switch (lex(par->lex, &tok)) {
		case T_EXPORT:
			(*next)->decl.exported = true;
			trace(TR_PARSE, "export");
			break;
		default:
			unlex(par->lex, &tok);
			break;
		}
		parse_decl(par, &(*next)->decl);
		next = &(*next)->next;
		*next = calloc(1, sizeof(struct ast_decls));
		want(par, T_SEMICOLON, NULL);
		if (lex(par->lex, &tok) != T_EOF) {
			unlex(par->lex, &tok);
		}
	}
	free(*next);
	*next = 0;
	trleave(TR_PARSE, NULL);
}

void
parse(struct lexer *lex, struct ast_subunit *subunit)
{
	struct parser par = {
		.lex = lex,
	};
	parse_imports(&par, subunit);
	parse_decls(&par, &subunit->decls);
	want(&par, T_EOF, NULL);
}
