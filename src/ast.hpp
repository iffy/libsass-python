#define SASS_AST

#include <string>
#include <sstream>
#include <vector>

#ifndef SASS_OPERATION
#include "operation.hpp"
#endif

#ifndef SASS_TOKEN
#include "token.hpp"
#endif

#include "ast_def_macros.hpp"

namespace Sass {
  using namespace std;

  /////////////////////////////////////////////////////////////////////////////
  // Mixin class for AST nodes that should behave like vectors. Uses the
  // "Template Method" design pattern to allow subclasses to adjust their flags
  // when certain objects are pushed.
  /////////////////////////////////////////////////////////////////////////////
  template <typename T>
  class Vectorized {
    vector<T> elements_;
  protected:
    virtual void adjust_after_pushing(T element) { }
  public:
    Vectorized(size_t s = 0) : elements_(vector<T>())
    { elements_.reserve(s); }
    virtual ~Vectorized() = 0;
    size_t length() const   { return elements_.size(); }
    bool empty() const      { return elements_.empty(); }
    T& operator[](size_t i) { return elements_[i]; }
    Vectorized& operator<<(T element)
    {
      elements_.push_back(element);
      adjust_after_pushing(element);
      return *this;
    }
    Vectorized& operator+=(Vectorized* v)
    {
      for (size_t i = 0, L = v->length(); i < L; ++i) *this << (*v)[i];
      return *this;
    }
  };
  template <typename T>
  inline Vectorized<T>::~Vectorized() { }

  //////////////////////////////////////////////////////////
  // Abstract base class for all abstract syntax tree nodes.
  //////////////////////////////////////////////////////////
  class AST_Node {
    ADD_PROPERTY(string, path);
    ADD_PROPERTY(size_t, line);
  public:
    AST_Node(string p, size_t l) : path_(p), line_(l) { }
    virtual ~AST_Node() = 0;
    ATTACH_OPERATIONS();
  };
  inline AST_Node::~AST_Node() { }

  /////////////////////////////////////////////////////////////////////////
  // Abstract base class for statements. This side of the AST hierarchy
  // represents elements in expansion contexts, which exist primarily to be
  // rewritten and macro-expanded.
  /////////////////////////////////////////////////////////////////////////
  class Statement : public AST_Node {
  public:
    Statement(string p, size_t l) : AST_Node(p, l) { }
    virtual ~Statement() = 0;
    // needed for rearranging nested rulesets during CSS emission
    virtual bool is_hoistable() { return false; }
  };
  inline Statement::~Statement() { }

  ////////////////////////
  // Blocks of statements.
  ////////////////////////
  class Block : public Statement, public Vectorized<Statement*> {
    ADD_PROPERTY(bool, is_root);
    // needed for properly formatted CSS emission
    ADD_PROPERTY(bool, has_hoistable);
    ADD_PROPERTY(bool, has_non_hoistable);
  protected:
    void adjust_after_pushing(Statement* s)
    {
      if (s->is_hoistable()) has_hoistable_     = true;
      else                   has_non_hoistable_ = false;
    };
  public:
    Block(string p, size_t l, size_t s = 0, bool r = false)
    : Statement(p, l),
      Vectorized(s),
      is_root_(r), has_hoistable_(false), has_non_hoistable_(false)
    { }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////////////////////////////
  // Abstract base class for statements that contain blocks of statements.
  ////////////////////////////////////////////////////////////////////////
  class Has_Block : public Statement {
    ADD_PROPERTY(Block*, block);
  public:
    Has_Block(string p, size_t l, Block* b)
    : Statement(p, l), block_(b)
    { }
    virtual ~Has_Block() = 0;
  };
  inline Has_Block::~Has_Block() { }

  /////////////////////////////////////////////////////////////////////////////
  // Rulesets (i.e., sets of styles headed by a selector and containing a block
  // of style declarations.
  /////////////////////////////////////////////////////////////////////////////
  class Selector;
  class Ruleset : public Has_Block {
    ADD_PROPERTY(Selector*, selector);
  public:
    Ruleset(string p, size_t l, Selector* s, Block* b)
    : Has_Block(p, l, b), selector_(s)
    { }
    // nested rulesets need to be hoisted out of their enclosing blocks
    bool is_hoistable() { return true; }
    ATTACH_OPERATIONS();
  };

  /////////////////////////////////////////////////////////
  // Nested declaration sets (i.e., namespaced properties).
  /////////////////////////////////////////////////////////
  class String;
  class Propset : public Has_Block {
    ADD_PROPERTY(String*, property_fragment);
  public:
    Propset(string p, size_t l, String* pf, Block* b)
    : Has_Block(p, l, b), property_fragment_(pf)
    { }
    ATTACH_OPERATIONS();
  };

  /////////////////
  // Media queries.
  /////////////////
  class List;
  class Media_Block : public Has_Block {
    ADD_PROPERTY(List*, media_queries);
  public:
    Media_Block(string p, size_t l, List* mqs, Block* b)
    : Has_Block(p, l, b), media_queries_(mqs)
    { }
    ATTACH_OPERATIONS();
  };

  ///////////////////////////////////////////////////////////////////////
  // At-rules -- arbitrary directives beginning with "@" that may have an
  // optional statement block.
  ///////////////////////////////////////////////////////////////////////
  class At_Rule : public Has_Block {
    ADD_PROPERTY(string, keyword);
    ADD_PROPERTY(Selector*, selector);
  public:
    At_Rule(string p, size_t l, string kwd, Selector* sel, Block* b)
    : Has_Block(p, l, b), keyword_(kwd), selector_(sel)
    { }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////////////////////////////
  // Declarations -- style rules consisting of a property name and values.
  ////////////////////////////////////////////////////////////////////////
  class Declaration : public Statement {
    ADD_PROPERTY(String*, property);
    ADD_PROPERTY(Expression*, value);
    ADD_PROPERTY(bool, is_important);
  public:
    Declaration(string p, size_t l,
                String* prop, Expression* val, bool i = false)
    : Statement(p, l), property_(prop), value_(val), is_important_(i)
    { }
    ATTACH_OPERATIONS();
  };

  /////////////////////////////////////
  // Assignments -- variable and value.
  /////////////////////////////////////
  class Variable;
  class Expression;
  class Assignment : public Statement {
    ADD_PROPERTY(string, variable);
    ADD_PROPERTY(Expression*, value);
    ADD_PROPERTY(bool, is_guarded);
  public:
    Assignment(string p, size_t l,
               string var, Expression* val, bool guarded = false)
    : Statement(p, l), variable_(var), value_(val), is_guarded_(guarded)
    { }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////////////////////////////////
  // Import directives. CSS and Sass import lists can be intermingled, so it's
  // necessary to store a list of each in an Import node.
  ////////////////////////////////////////////////////////////////////////////
  class Import : public Statement {
    vector<string>         files_;
    vector<Function_Call*> urls_;
  public:
    Import(string p, size_t l)
    : Statement(p, l),
      files_(vector<string>()), urls_(vector<Function_Call*>())
    { }
    vector<string>&         files()   { return files_; }
    vector<Function_Call*>& urls()    { return urls_; }
    ATTACH_OPERATIONS();
  };

  class Import_Stub : public Statement {
    ADD_PROPERTY(string, file_name);
  public:
    Import_Stub(string p, size_t l, string f)
    : Statement(p, l), file_name_(f)
    { }
    ATTACH_OPERATIONS();
  };

  //////////////////////////////
  // The Sass `@warn` directive.
  //////////////////////////////
  class Warning : public Statement {
    ADD_PROPERTY(Expression*, message);
  public:
    Warning(string p, size_t l, Expression* msg)
    : Statement(p, l), message_(msg)
    { }
    ATTACH_OPERATIONS();
  };

  ///////////////////////////////////////////
  // CSS comments. These may be interpolated.
  ///////////////////////////////////////////
  class Comment : public Statement {
    ADD_PROPERTY(String*, text);
  public:
    Comment(string p, size_t l, String* txt)
    : Statement(p, l), text_(txt)
    { }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////
  // The Sass `@if` control directive.
  ////////////////////////////////////
  class If : public Statement {
    ADD_PROPERTY(Expression*, predicate);
    ADD_PROPERTY(Block*, consequent);
    ADD_PROPERTY(Block*, alternative);
  public:
    If(string p, size_t l, Expression* pred, Block* con, Block* alt = 0)
    : Statement(p, l), predicate_(pred), consequent_(con), alternative_(alt)
    { }
    ATTACH_OPERATIONS();
  };

  /////////////////////////////////////
  // The Sass `@for` control directive.
  /////////////////////////////////////
  class For : public Has_Block {
    ADD_PROPERTY(string, variable);
    ADD_PROPERTY(Expression*, lower_bound);
    ADD_PROPERTY(Expression*, upper_bound);
    ADD_PROPERTY(bool, is_inclusive);
  public:
    For(string p, size_t l,
        string var, Expression* lo, Expression* hi, Block* b, bool inc)
    : Has_Block(p, l, b),
      variable_(var), lower_bound_(lo), upper_bound_(hi), is_inclusive_(inc)
    { }
    ATTACH_OPERATIONS();
  };

  //////////////////////////////////////
  // The Sass `@each` control directive.
  //////////////////////////////////////
  class Each : public Has_Block {
    ADD_PROPERTY(string, variable);
    ADD_PROPERTY(Expression*, list);
  public:
    Each(string p, size_t l, string var, Expression* lst, Block* b)
    : Has_Block(p, l, b), variable_(var), list_(lst)
    { }
    ATTACH_OPERATIONS();
  };

  ///////////////////////////////////////
  // The Sass `@while` control directive.
  ///////////////////////////////////////
  class While : public Has_Block {
    ADD_PROPERTY(Expression*, predicate);
  public:
    While(string p, size_t l, Expression* pred, Block* b)
    : Has_Block(p, l, b), predicate_(pred)
    { }
    ATTACH_OPERATIONS();
  };

  /////////////////////////////////////////////////////////////
  // The @return directive for use inside SassScript functions.
  /////////////////////////////////////////////////////////////
  class Return : public Statement {
    ADD_PROPERTY(Expression*, value);
  public:
    Return(string p, size_t l, Expression* val = 0)
    : Statement(p, l), value_(val)
    { }
  };

  ///////////////////////////////////////////////////
  // The @content directive for mixin content blocks.
  ///////////////////////////////////////////////////
  class Content : public Statement {
  public:
    Content(string p, size_t l) : Statement(p, l) { }
  };

  ////////////////////////////////
  // The Sass `@extend` directive.
  ////////////////////////////////
  class Extend : public Statement {
    ADD_PROPERTY(Selector*, selector);
  public:
    Extend(string p, size_t l, Selector* s)
    : Statement(p, l), selector_(s)
    { }
    ATTACH_OPERATIONS();
  };

  /////////////////////////////////////////////////////////////////////////////
  // Definitions for both mixins and functions. The two cases are distinguished
  // by a type tag.
  /////////////////////////////////////////////////////////////////////////////
  class Parameters;
  class Definition : public Has_Block {
  public:
    enum Type { MIXIN, FUNCTION };
    ADD_PROPERTY(string, name);
    ADD_PROPERTY(Parameters*, parameters);
    ADD_PROPERTY(Type, type);
  public:
    Definition(string p, size_t l,
               string n, Parameters* params, Block* b, Type t)
    : Has_Block(p, l, b), name_(n), parameters_(params), type_(t)
    { }
    ATTACH_OPERATIONS();
  };

  //////////////////////////////////////
  // Mixin calls (i.e., `@include ...`).
  //////////////////////////////////////
  class Arguments;
  class Mixin_Call : public Has_Block {
    ADD_PROPERTY(string, name);
    ADD_PROPERTY(Arguments*, arguments);
  public:
    Mixin_Call(string p, size_t l, string n, Arguments* args, Block* b = 0)
    : Has_Block(p, l, b), name_(n), arguments_(args)
    { }
    ATTACH_OPERATIONS();
  };

  //////////////////////////////////////////////////////////////////////
  // Abstract base class for expressions. This side of the AST hierarchy
  // represents elements in value contexts, which exist primarily to be
  // evaluated and returned.
  //////////////////////////////////////////////////////////////////////
  class Expression : public AST_Node {
    // expressions in some contexts shouldn't be evaluated
    ADD_PROPERTY(bool, is_delayed);
    // for media features, if I recall
    ADD_PROPERTY(bool, is_parenthesized);
  public:
    Expression(string p, size_t l)
    : AST_Node(p, l), is_delayed_(false), is_parenthesized_(false)
    { }
    virtual ~Expression() = 0;
    virtual string type() { return ""; /* TODO: raise an error */ }
  };
  inline Expression::~Expression() { }

  ///////////////////////////////////////////////////////////////////////
  // Lists of values, both comma- and space-separated (distinguished by a
  // type-tag.) Also used to represent variable-length argument lists.
  ///////////////////////////////////////////////////////////////////////
  class List : public Expression, public Vectorized<Expression*> {
  public:
    enum Separator { space, comma };
  private:
    ADD_PROPERTY(Separator, separator);
    ADD_PROPERTY(bool, is_arglist);
  public:
    List(string p, size_t l,
         size_t size = 0, Separator sep = space, bool argl = false)
    : Expression(p, l),
      Vectorized(size),
      separator_(sep), is_arglist_(argl)
    { }
    string type() { return is_arglist_ ? "arglist" : "list"; }
    ATTACH_OPERATIONS();
  };

  //////////////////////////////////////////////////////////////////////////
  // Binary expressions. Represents logical, relational, and arithmetic
  // operations. Templatized to avoid large switch statements and repetitive
  // subclassing.
  //////////////////////////////////////////////////////////////////////////
  class Binary_Expression : public Expression {
  public:
    enum Type {
      AND, OR,                   // logical connectives
      EQ, NEQ, GT, GTE, LT, LTE, // arithmetic relations
      ADD, SUB, MUL, DIV, MOD    // arithmetic functions
    };
  private:
    ADD_PROPERTY(Type, type);
    ADD_PROPERTY(Expression*, left);
    ADD_PROPERTY(Expression*, right);
  public:
    Binary_Expression(string p, size_t l,
                      Type t, Expression* lhs, Expression* rhs)
    : Expression(p, l), type_(t), left_(lhs), right_(rhs)
    { }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////////////////////////////////
  // Arithmetic negation (logical negation is just an ordinary function call).
  ////////////////////////////////////////////////////////////////////////////
  class Unary_Expression : public Expression {
  public:
    enum Type { PLUS, MINUS };
  private:
    ADD_PROPERTY(Type, type);
    ADD_PROPERTY(Expression*, operand);
  public:
    Unary_Expression(string p, size_t l, Type t, Expression* o)
    : Expression(p, l), type_(t), operand_(o)
    { }
    ATTACH_OPERATIONS();
  };

  //////////////////
  // Function calls.
  //////////////////
  class Function_Call : public Expression {
    ADD_PROPERTY(string, name);
    ADD_PROPERTY(Arguments*, arguments);
  public:
    Function_Call(string p, size_t l, string n, Arguments* args)
    : Expression(p, l), name_(n), arguments_(args)
    { }
    ATTACH_OPERATIONS();
  };

  ///////////////////////
  // Variable references.
  ///////////////////////
  class Variable : public Expression {
    ADD_PROPERTY(string, name);
  public:
    Variable(string p, size_t l, string n)
    : Expression(p, l), name_(n)
    { }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////////////////////////////////
  // Textual (i.e., unevaluated) numeric data. Variants are distinguished with
  // a type tag.
  ////////////////////////////////////////////////////////////////////////////
  class Textual : public Expression {
  public:
    enum Type { NUMBER, PERCENTAGE, DIMENSION, HEX };
  private:
    ADD_PROPERTY(Type, type);
    ADD_PROPERTY(string, value);
  public:
    Textual(string p, size_t l, Type t, string val)
    : Expression(p, l), type_(t), value_(val)
    { }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////
  // Numbers, percentages, dimensions, and colors.
  ////////////////////////////////////////////////
  class Numeric : public Expression {
    ADD_PROPERTY(double, value);
  public:
    Numeric(string p, size_t l, double val) : Expression(p, l), value_(val) { }
    virtual ~Numeric() = 0;
    string type() { return "number"; }
  };
  inline Numeric::~Numeric() { }
  class Number : public Numeric {
    Number(string p, size_t l, double val) : Numeric(p, l, val) { }
    ATTACH_OPERATIONS();
  };
  class Percentage : public Numeric {
    Percentage(string p, size_t l, double val) : Numeric(p, l, val) { }
    ATTACH_OPERATIONS();
  };
  class Dimension : public Numeric {
    vector<string> numerator_units_;
    vector<string> denominator_units_;
  public:
    Dimension(string p, size_t l, double val, string unit)
    : Numeric(p, l, val),
      numerator_units_(vector<string>()),
      denominator_units_(vector<string>())
    { numerator_units_.push_back(unit); }
    vector<string>& numerator_units()   { return numerator_units_; }
    vector<string>& denominator_units() { return denominator_units_; }
    ATTACH_OPERATIONS();
  };

  //////////
  // Colors.
  //////////
  class Color : public Expression {
    ADD_PROPERTY(double, r);
    ADD_PROPERTY(double, g);
    ADD_PROPERTY(double, b);
    ADD_PROPERTY(double, a);
  public:
    Color(string p, size_t l, double r, double g, double b, double a = 1)
    : Expression(p, l), r_(r), g_(g), b_(b), a_(a)
    { }
    string type() { return "color"; }
    ATTACH_OPERATIONS();
  };

  ////////////
  // Booleans.
  ////////////
  class Boolean : public Expression {
    ADD_PROPERTY(bool, value);
  public:
    Boolean(string p, size_t l, bool val) : Expression(p, l), value_(val) { }
    string type() { return "bool"; }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////////////////////////////
  // Abstract base class for Sass string values. Includes interpolated and
  // "flat" strings.
  ////////////////////////////////////////////////////////////////////////
  class String : public Expression {
  public:
    String(string p, size_t l) : Expression(p, l) { }
    virtual ~String() = 0;
  };
  inline String::~String() { };

  ///////////////////////////////////////////////////////////////////////
  // Interpolated strings. Meant to be reduced to flat strings during the
  // evaluation phase.
  ///////////////////////////////////////////////////////////////////////
  class String_Schema : public String, public Vectorized<Expression*> {
  public:
    String_Schema(string p, size_t l, size_t size = 0)
    : String(p, l), Vectorized(size)
    { }
    string type() { return "string"; }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////////////
  // Flat strings -- the lowest level of raw textual data.
  ////////////////////////////////////////////////////////
  class String_Constant : public String {
    ADD_PROPERTY(string, value);
  public:
    String_Constant(string p, size_t l, string val)
    : String(p, l), value_(val)
    { }
    String_Constant(string p, size_t l, const char* beg)
    : String(p, l), value_(string(beg))
    { }
    String_Constant(string p, size_t l, const char* beg, const char* end)
    : String(p, l), value_(string(beg, end-beg))
    { }
    String_Constant(string p, size_t l, const Token& tok)
    : String(p, l), value_(string(tok.begin, tok.end))
    { }
    string type() { return "string"; }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////////
  // Media expressions (for use inside media queries).
  ////////////////////////////////////////////////////
  class Media_Query_Expression : public Expression {
    ADD_PROPERTY(String*, feature);
    ADD_PROPERTY(Expression*, value);
  public:
    Media_Query_Expression(string p, size_t l, String* f, Expression* v)
    : Expression(p, l), feature_(f), value_(v)
    { }
  };

  /////////////////////////////////////////////////////////
  // Individual parameter objects for mixins and functions.
  /////////////////////////////////////////////////////////
  class Parameter : public AST_Node {
    ADD_PROPERTY(string, name);
    ADD_PROPERTY(Expression*, default_value);
    ADD_PROPERTY(bool, is_rest_parameter);
  public:
    Parameter(string p, size_t l,
              string n, Expression* def = 0, bool rest = false)
    : AST_Node(p, l), name_(n), default_value_(def), is_rest_parameter_(rest)
    { /* TO-DO: error if default_value && is_packed */ }
    ATTACH_OPERATIONS();
  };

  /////////////////////////////////////////////////////////////////////////
  // Parameter lists -- in their own class to facilitate context-sensitive
  // error checking (e.g., ensuring that all optional parameters follow all
  // required parameters).
  /////////////////////////////////////////////////////////////////////////
  class Parameters : public AST_Node, public Vectorized<Parameter*> {
    ADD_PROPERTY(bool, has_optional_parameters);
    ADD_PROPERTY(bool, has_rest_parameter);
  protected:
    void adjust_after_pushing(Parameter* p)
    {
      if (p->default_value()) {
        if (has_rest_parameter_)
          ; // error
        has_optional_parameters_ = true;
      }
      else if (p->is_rest_parameter()) {
        if (has_rest_parameter_)
          ; // error
        if (has_optional_parameters_)
          ; // different error
        has_rest_parameter_ = true;
      }
      else {
        if (has_rest_parameter_)
          ; // error
        if (has_optional_parameters_)
          ; // different error
      }
    }
  public:
    Parameters(string p, size_t l)
    : AST_Node(p, l),
      Vectorized(),
      has_optional_parameters_(false),
      has_rest_parameter_(false)
    { }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////////////////
  // Individual argument objects for mixin and function calls.
  ////////////////////////////////////////////////////////////
  class Argument : public AST_Node {
    ADD_PROPERTY(Expression*, value);
    ADD_PROPERTY(string, name);
    ADD_PROPERTY(bool, is_rest_argument);
  public:
    Argument(string p, size_t l, Expression* val, string n = "", bool rest = false)
    : AST_Node(p, l), value_(val), name_(n), is_rest_argument_(rest)
    { if (name_ != "" && is_rest_argument_) { /* error */ } }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////////////////////////////
  // Argument lists -- in their own class to facilitate context-sensitive
  // error checking (e.g., ensuring that all ordinal arguments precede all
  // named arguments).
  ////////////////////////////////////////////////////////////////////////
  class Arguments : public AST_Node, public Vectorized<Argument*> {
    ADD_PROPERTY(bool, has_named_arguments);
    ADD_PROPERTY(bool, has_rest_argument);
  protected:
    void adjust_after_pushing(Argument* a)
    {
      if (!a->name().empty()) {
        if (has_rest_argument_)
          ; // error
        has_named_arguments_ = true;
      }
      else if (a->is_rest_argument()) {
        if (has_rest_argument_)
          ; // error
        if (has_named_arguments_)
          ; // different error
        has_rest_argument_ = true;
      }
      else {
        if (has_rest_argument_)
          ; // error
        if (has_named_arguments_)
          ; // error
      }
    }
  public:
    Arguments(string p, size_t l)
    : AST_Node(p, l),
      Vectorized(),
      has_named_arguments_(false),
      has_rest_argument_(false)
    { }
    ATTACH_OPERATIONS();
  };

  /////////////////////////////////////////
  // Abstract base class for CSS selectors.
  /////////////////////////////////////////
  class Selector : public AST_Node {
    ADD_PROPERTY(bool, has_reference);
    ADD_PROPERTY(bool, has_placeholder);
  public:
    Selector(string p, size_t l, bool r = false, bool h = false)
    : AST_Node(p, l), has_reference_(r), has_placeholder_(h)
    { }
    virtual ~Selector() = 0;
  };
  inline Selector::~Selector() { }

  /////////////////////////////////////////////////////////////////////////
  // Interpolated selectors -- the interpolated String will be expanded and
  // re-parsed into a normal selector classure.
  /////////////////////////////////////////////////////////////////////////
  class Selector_Schema : public Selector {
    ADD_PROPERTY(String*, contents);
  public:
    Selector_Schema(string p, size_t l, String* c)
    : Selector(p, l), contents_(c)
    { }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////
  // Abstract base class for simple selectors.
  ////////////////////////////////////////////
  class Simple_Selector : public Selector {
  public:
    Simple_Selector(string p, size_t l)
    : Selector(p, l)
    { }
    virtual ~Simple_Selector() = 0;
  };
  inline Simple_Selector::~Simple_Selector() { }

  /////////////////////////////////////
  // Parent references (i.e., the "&").
  /////////////////////////////////////
  class Selector_Reference : public Simple_Selector {
    ADD_PROPERTY(Selector*, selector);
  public:
    Selector_Reference(string p, size_t l)
    : Simple_Selector(p, l)
    { has_reference(true); }
    ATTACH_OPERATIONS();
  };

  /////////////////////////////////////////////////////////////////////////
  // Placeholder selectors (e.g., "%foo") for use in extend-only selectors.
  /////////////////////////////////////////////////////////////////////////
  class Selector_Placeholder : public Simple_Selector {
    ADD_PROPERTY(string, name);
  public:
    Selector_Placeholder(string p, size_t l, string n)
    : Simple_Selector(p, l), name_(n)
    { has_placeholder(true); }
    ATTACH_OPERATIONS();
  };

  /////////////////////////////////////////////////////////////////////
  // Type selectors (and the universal selector) -- e.g., div, span, *.
  /////////////////////////////////////////////////////////////////////
  class Type_Selector : public Simple_Selector {
    ADD_PROPERTY(string, name);
  public:
    Type_Selector(string p, size_t l, string n)
    : Simple_Selector(p, l), name_(n)
    { }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////
  // Selector qualifiers -- i.e., classes and ids.
  ////////////////////////////////////////////////
  class Selector_Qualifier : public Simple_Selector {
    ADD_PROPERTY(string, name);
  public:
    Selector_Qualifier(string p, size_t l, string n)
    : Simple_Selector(p, l), name_(n)
    { }
    ATTACH_OPERATIONS();
  };

  ///////////////////////////////////////////////////
  // Attribute selectors -- e.g., [src*=".jpg"], etc.
  ///////////////////////////////////////////////////
  class Attribute_Selector : public Simple_Selector {
    ADD_PROPERTY(string, name);
    ADD_PROPERTY(string, matcher);
    ADD_PROPERTY(string, value);
  public:
    Attribute_Selector(string p, size_t l, string n, string m, string v)
    : Simple_Selector(p, l), name_(n), matcher_(m), value_(v)
    { }
    ATTACH_OPERATIONS();
  };

  //////////////////////////////////////////////////////////////////
  // Pseudo selectors -- e.g., :first-child, :nth-of-type(...), etc.
  //////////////////////////////////////////////////////////////////
  class Pseudo_Selector : public Simple_Selector {
    ADD_PROPERTY(string, name);
    ADD_PROPERTY(Expression*, expression);
  public:
    Pseudo_Selector(string p, size_t l, string n, Expression* expr = 0)
    : Simple_Selector(p, l), name_(n), expression_(expr)
    { }
    ATTACH_OPERATIONS();
  };

  /////////////////////////////////////////////////
  // Negated selector -- e.g., :not(:first-of-type)
  /////////////////////////////////////////////////
  class Negated_Selector : public Simple_Selector {
    ADD_PROPERTY(Simple_Selector*, selector);
  public:
    Negated_Selector(string p, size_t l, Simple_Selector* sel)
    : Simple_Selector(p, l), selector_(sel)
    { }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////////////////////////////////
  // Simple selector sequences. Maintains flags indicating whether it contains
  // any parent references or placeholders, to simplify expansion.
  ////////////////////////////////////////////////////////////////////////////
  class Simple_Selector_Sequence : public Selector, public Vectorized<Simple_Selector*> {
  protected:
    void adjust_after_pushing(Simple_Selector* s)
    {
      has_reference(has_reference() || s->has_reference());
      has_placeholder(has_placeholder() || s->has_placeholder());
    }
  public:
    Simple_Selector_Sequence(string p, size_t l, size_t s = 0)
    : Selector(p, l),
      Vectorized(s)
    { }
    ATTACH_OPERATIONS();
  };

  ////////////////////////////////////////////////////////////////////////////
  // General selectors -- i.e., simple sequences combined with one of the four
  // CSS selector combinators (">", "+", "~", and whitespace). Essentially a
  // linked list.
  ////////////////////////////////////////////////////////////////////////////
  class Selector_Combination : public Selector {
  public:
    enum Combinator { ANCESTOR_OF, PARENT_OF, PRECEDES, ADJACENT_TO };
  private:
    ADD_PROPERTY(Combinator, combinator);
    ADD_PROPERTY(Simple_Selector_Sequence*, head);
    ADD_PROPERTY(Selector_Combination*, tail);
    ADD_PROPERTY(bool, has_reference);
    ADD_PROPERTY(bool, has_placeholder);
  public:
    Selector_Combination(string p, size_t l,
                         Combinator c,
                         Simple_Selector_Sequence* h,
                         Selector_Combination* t)
    : Selector(p, l, h && h->has_reference() ||
                     t && t->has_reference(),
                     h && h->has_placeholder() ||
                     t && t->has_placeholder()),
      combinator_(c),
      head_(h),
      tail_(t)
    { }
    ATTACH_OPERATIONS();
  };

  ///////////////////////////////////
  // Comma-separated selector groups.
  ///////////////////////////////////
  class Selector_Group
      : public Selector, public Vectorized<Selector_Combination*> {
    ADD_PROPERTY(bool, has_reference);
    ADD_PROPERTY(bool, has_placeholder);
  protected:
    void adjust_after_pushing(Selector_Combination* c)
    {
      has_reference_   |= c->has_reference();
      has_placeholder_ |= c->has_placeholder();
    }
  public:
    Selector_Group(string p, size_t l, size_t s = 0)
    : Selector(p, l),
      Vectorized(s),
      has_reference_(false),
      has_placeholder_(false)
    { }
    ATTACH_OPERATIONS();
  };
}