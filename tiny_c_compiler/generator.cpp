/*
 * File:	generator.cpp
 *
 * Description:	Minimal AArch64 code generator for Simple C.
 *
 * Scope:
 *	- integer/pointer scalars
 *	- locals, parameters, globals
 *	- string literals
 *	- arithmetic, comparisons, assignment
 *	- if/while/for/break
 *	- direct function calls with up to 8 integer/pointer arguments
 *
 * This intentionally keeps the imported frontend while replacing the
 * original x86-64 SysV backend with an AArch64 backend suitable for the
 * QEMU `virt` / Raspberry Pi targets used by this repo.
 */

# include <cassert>
# include <cstdlib>
# include <iostream>
# include <map>
# include <string>
# include <vector>
# include "generator.h"
# include "machine.h"
# include "Tree.h"
# include "label.h"
# include "string.h"

using namespace std;

static int offset;
static string funcname;
static string func_exit_label;
static int frame_size;
static map<string, Label> strings;
static vector<Label> break_labels;

static void fail_codegen(const string &msg)
{
    cerr << "tiny_c_compiler: " << msg << endl;
    exit(EXIT_FAILURE);
}

static int align_to(int value, int alignment)
{
    int remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

static void emit_stack_push(const char *reg)
{
    cout << "\tstr\t" << reg << ", [sp, -16]!" << endl;
}

static void emit_stack_pop(const char *reg)
{
    cout << "\tldr\t" << reg << ", [sp], 16" << endl;
}

static void emit_bool_from_cond(const char *cond)
{
    cout << "\tcset\tx0, " << cond << endl;
}

static void emit_integer_constant(unsigned long value)
{
    cout << "\tldr\tx0, =" << value << endl;
}

static void emit_local_address(int local_offset)
{
    if (local_offset < 0)
        cout << "\tsub\tx0, x29, #" << -local_offset << endl;
    else if (local_offset > 0)
        cout << "\tadd\tx0, x29, #" << local_offset << endl;
    else
        cout << "\tmov\tx0, x29" << endl;
}

static void emit_global_address(const string &name)
{
    cout << "\tadrp\tx0, " << global_prefix << name << endl;
    cout << "\tadd\tx0, x0, :lo12:" << global_prefix << name << endl;
}

static void emit_load_from_address(const Type &type)
{
    if (type.size() == 1)
        cout << "\tldrsb\tx0, [x0]" << endl;
    else if (type.size() == 4)
        cout << "\tldrsw\tx0, [x0]" << endl;
    else
        cout << "\tldr\tx0, [x0]" << endl;
}

static void emit_store_to_address(const Type &type)
{
    if (type.size() == 1)
        cout << "\tstrb\tw0, [x1]" << endl;
    else if (type.size() == 4)
        cout << "\tstr\tw0, [x1]" << endl;
    else
        cout << "\tstr\tx0, [x1]" << endl;
}

static void emit_load_symbol(const Symbol *symbol)
{
    if (symbol->offset == 0)
        emit_global_address(symbol->name());
    else
        emit_local_address(symbol->offset);

    emit_load_from_address(symbol->type());
}

static void emit_lvalue_address(Expression *expr)
{
    Expression *pointer = nullptr;

    if (auto ident = dynamic_cast<Identifier *>(expr)) {
        const Symbol *symbol = ident->symbol();
        if (symbol->offset == 0)
            emit_global_address(symbol->name());
        else
            emit_local_address(symbol->offset);
        return;
    }

    if (auto str = dynamic_cast<String *>(expr)) {
        str->generate();
        return;
    }

    if (expr->isDereference(pointer)) {
        pointer->generate();
        return;
    }

    fail_codegen("unsupported lvalue expression");
}

static void emit_compare(Expression *left, Expression *right, const char *cond)
{
    left->generate();
    emit_stack_push("x0");
    right->generate();
    emit_stack_pop("x1");
    cout << "\tcmp\tx1, x0" << endl;
    emit_bool_from_cond(cond);
}


/*
 * The imported frontend still expects these helper methods on the AST.
 */

void Expression::operand(ostream &ostr) const
{
    ostr << "<operand>";
}

void Expression::test(const Label &label, bool ifTrue)
{
    generate();
    cout << "\tcmp\tx0, #0" << endl;
    cout << "\tb." << (ifTrue ? "ne" : "eq") << "\t" << label << endl;
}


/*
 * Basic operand printing helpers kept for compatibility with the old design.
 */

void Identifier::operand(ostream &ostr) const
{
    if (_symbol->offset == 0)
        ostr << global_prefix << _symbol->name() << global_suffix;
    else
        ostr << _symbol->offset << "(fp)";
}

void Number::operand(ostream &ostr) const
{
    ostr << "#" << _value;
}

void String::operand(ostream &ostr) const
{
    auto it = strings.emplace(_value, Label()).first;
    ostr << it->second;
}


/*
 * Expression codegen.
 */

void String::generate()
{
    auto it = strings.emplace(_value, Label()).first;
    cout << "\tadrp\tx0, " << it->second << endl;
    cout << "\tadd\tx0, x0, :lo12:" << it->second << endl;
}

void Identifier::generate()
{
    emit_load_symbol(_symbol);
}

void Number::generate()
{
    emit_integer_constant(strtoul(_value.c_str(), nullptr, 0));
}

void Call::generate()
{
    if (_args.size() > NUM_PARAM_REGS)
        fail_codegen("more than 8 call arguments are not supported yet");

    for (int i = (int)_args.size() - 1; i >= 0; i--) {
        _args[i]->generate();
        emit_stack_push("x0");
    }

    for (unsigned i = 0; i < _args.size(); i++) {
        string reg = "x" + to_string(i);
        cout << "\tldr\t" << reg << ", [sp], 16" << endl;
    }

    cout << "\tbl\t" << global_prefix << _id->name() << endl;
}

void Not::generate()
{
    _expr->generate();
    cout << "\tcmp\tx0, #0" << endl;
    emit_bool_from_cond("eq");
}

void Negate::generate()
{
    _expr->generate();
    cout << "\tneg\tx0, x0" << endl;
}

void Dereference::generate()
{
    _expr->generate();
    emit_load_from_address(type());
}

void Address::generate()
{
    emit_lvalue_address(_expr);
}

void Cast::generate()
{
    _expr->generate();

    if (type().size() == 1)
        cout << "\tsxtb\tx0, w0" << endl;
    else if (type().size() == 4)
        cout << "\tsxtw\tx0, w0" << endl;
}

void Multiply::generate()
{
    _left->generate();
    emit_stack_push("x0");
    _right->generate();
    emit_stack_pop("x1");
    cout << "\tmul\tx0, x1, x0" << endl;
}

void Divide::generate()
{
    _left->generate();
    emit_stack_push("x0");
    _right->generate();
    emit_stack_pop("x1");
    cout << "\tsdiv\tx0, x1, x0" << endl;
}

void Remainder::generate()
{
    _left->generate();
    emit_stack_push("x0");
    _right->generate();
    emit_stack_pop("x1");
    cout << "\tsdiv\tx2, x1, x0" << endl;
    cout << "\tmsub\tx0, x2, x0, x1" << endl;
}

void Add::generate()
{
    _left->generate();
    emit_stack_push("x0");
    _right->generate();
    emit_stack_pop("x1");
    cout << "\tadd\tx0, x1, x0" << endl;
}

void Subtract::generate()
{
    _left->generate();
    emit_stack_push("x0");
    _right->generate();
    emit_stack_pop("x1");
    cout << "\tsub\tx0, x1, x0" << endl;
}

void LessThan::generate()
{
    emit_compare(_left, _right, "lt");
}

void GreaterThan::generate()
{
    emit_compare(_left, _right, "gt");
}

void LessOrEqual::generate()
{
    emit_compare(_left, _right, "le");
}

void GreaterOrEqual::generate()
{
    emit_compare(_left, _right, "ge");
}

void Equal::generate()
{
    emit_compare(_left, _right, "eq");
}

void NotEqual::generate()
{
    emit_compare(_left, _right, "ne");
}

void LogicalAnd::generate()
{
    Label false_label, done_label;
    _left->test(false_label, false);
    _right->test(false_label, false);
    cout << "\tmov\tx0, #1" << endl;
    cout << "\tb\t" << done_label << endl;
    cout << false_label << ":" << endl;
    cout << "\tmov\tx0, #0" << endl;
    cout << done_label << ":" << endl;
}

void LogicalOr::generate()
{
    Label true_label, done_label;
    _left->test(true_label, true);
    _right->test(true_label, true);
    cout << "\tmov\tx0, #0" << endl;
    cout << "\tb\t" << done_label << endl;
    cout << true_label << ":" << endl;
    cout << "\tmov\tx0, #1" << endl;
    cout << done_label << ":" << endl;
}


/*
 * Statement codegen.
 */

void Assignment::generate()
{
    _right->generate();
    emit_stack_push("x0");
    emit_lvalue_address(_left);
    cout << "\tmov\tx1, x0" << endl;
    emit_stack_pop("x0");
    emit_store_to_address(_left->type());
}

void Break::generate()
{
    if (break_labels.empty())
        fail_codegen("break used outside a loop");

    cout << "\tb\t" << break_labels.back() << endl;
}

void Return::generate()
{
    if (_expr != nullptr)
        _expr->generate();
    else
        cout << "\tmov\tx0, #0" << endl;

    cout << "\tb\t" << func_exit_label << endl;
}

void Block::generate()
{
    for (auto stmt : _stmts)
        stmt->generate();
}

void While::generate()
{
    Label loop_label, done_label;
    break_labels.push_back(done_label);

    cout << loop_label << ":" << endl;
    _expr->test(done_label, false);
    _stmt->generate();
    cout << "\tb\t" << loop_label << endl;
    cout << done_label << ":" << endl;

    break_labels.pop_back();
}

void For::generate()
{
    Label loop_label, done_label;
    break_labels.push_back(done_label);

    if (_init != nullptr)
        _init->generate();

    cout << loop_label << ":" << endl;
    if (_expr != nullptr)
        _expr->test(done_label, false);
    if (_stmt != nullptr)
        _stmt->generate();
    if (_incr != nullptr)
        _incr->generate();
    cout << "\tb\t" << loop_label << endl;
    cout << done_label << ":" << endl;

    break_labels.pop_back();
}

void If::generate()
{
    Label else_label, done_label;

    _expr->test(else_label, false);
    _thenStmt->generate();

    if (_elseStmt != nullptr) {
        cout << "\tb\t" << done_label << endl;
        cout << else_label << ":" << endl;
        _elseStmt->generate();
        cout << done_label << ":" << endl;
    } else {
        cout << else_label << ":" << endl;
    }
}

void Simple::generate()
{
    if (_expr != nullptr)
        _expr->generate();
}

void Function::generate()
{
    Types types = _id->type().parameters()->types;
    Symbols symbols = _body->declarations()->symbols();

    if (types.size() > NUM_PARAM_REGS)
        fail_codegen("functions with more than 8 parameters are not supported yet");

    offset = 0;
    allocate(offset);
    frame_size = align_to(-offset, STACK_ALIGNMENT);

    funcname = _id->name();
    func_exit_label = ".L" + funcname + "_exit";

    cout << "\t.text" << endl;
    cout << "\t.align\t2" << endl;
    cout << "\t.global\t" << global_prefix << funcname << endl;
    cout << global_prefix << funcname << ":" << endl;
    cout << "\tstp\tx29, x30, [sp, -16]!" << endl;
    cout << "\tmov\tx29, sp" << endl;
    if (frame_size > 0)
        cout << "\tsub\tsp, sp, #" << frame_size << endl;

    for (unsigned i = 0; i < types.size(); i++) {
        const Symbol *symbol = symbols[i];
        int symoff = symbol->offset;

        if (symoff < 0) {
            if (types[i].size() == 1)
                cout << "\tstrb\tw" << i << ", [x29, #-" << -symoff << "]" << endl;
            else if (types[i].size() == 4)
                cout << "\tstr\tw" << i << ", [x29, #-" << -symoff << "]" << endl;
            else
                cout << "\tstr\tx" << i << ", [x29, #-" << -symoff << "]" << endl;
        } else if (symoff > 0) {
            if (types[i].size() == 1)
                cout << "\tstrb\tw" << i << ", [x29, #" << symoff << "]" << endl;
            else if (types[i].size() == 4)
                cout << "\tstr\tw" << i << ", [x29, #" << symoff << "]" << endl;
            else
                cout << "\tstr\tx" << i << ", [x29, #" << symoff << "]" << endl;
        } else {
            fail_codegen("parameter assigned invalid frame offset 0");
        }
    }

    _body->generate();

    cout << func_exit_label << ":" << endl;
    if (frame_size > 0)
        cout << "\tadd\tsp, sp, #" << frame_size << endl;
    cout << "\tldp\tx29, x30, [sp], 16" << endl;
    cout << "\tret" << endl << endl;
}


/*
 * Global emission.
 */

void generateGlobals(Scope *scope)
{
    const Symbols &symbols = scope->symbols();

    for (auto symbol : symbols) {
        if (!symbol->type().isFunction()) {
            cout << "\t.comm\t" << global_prefix << symbol->name();
            cout << ", " << symbol->type().size();
            cout << ", " << symbol->type().alignment() << endl;
        }
    }

    if (!strings.empty()) {
        cout << "\t.section\t.rodata" << endl;
        for (const auto &entry : strings) {
            cout << entry.second << ":" << endl;
            cout << "\t.asciz\t\"" << escapeString(entry.first) << "\"" << endl;
        }
    }
}
