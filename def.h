#define STRUCT_TABLE_SIZE			50
#define MAX_STRUCT_MEMBERS			10
#define STRING_TABLE_SIZE			250
#define MAX_USER_FUNC				50
#define MAX_USER_FUNC_CALLS			1000
#define MAX_USER_FUNC_PARAMS		10
#define MAX_GLOBAL_VARS				50
#define MAX_LOCAL_VARS				200
#define ID_LEN						50
#define MAX_MATRIX_DIMS				10
#define CONST_LEN					500
#define PROG_SIZE					99999
#define ACTIVE_FUNC_TABLE_SIZE		250
#define ACTIVE_CONST_TABLE_SIZE		250

#define TRUE						1
#define FALSE						0

struct s1{
	int i;
};
struct hah{
	struct s1 hey[100];
};

typedef void(*_FPTR)(void);

typedef struct {
	char *func_name;
	_FPTR fp;
} _LIB;

typedef enum {
	DIRECTIVE = 1, INCLUDE,
	
	VOID, CHAR, INT, FLOAT, DOUBLE,
	SHORT, LONG, SIGNED, UNSIGNED,
	
	STRUCT, STRUCT_DOT, STRUCT_ARROW,
	ENUM,
	
	IF, THEN, ELSE, FOR, DO, WHILE, BREAK, CONTINUE, SWITCH, CASE, DEFAULT, RETURN, CONST,
	SIZEOF,
	
	PLUS, MINUS, ASTERISK, DIVISION, INCREMENT, DECREMENT, MOD,
	
	EQUAL, NOT_EQUAL, LESS_THAN, LESS_THAN_OR_EQUAL, GREATER_THAN, GREATER_THAN_OR_EQUAL,
	LOGICAL_AND, LOGICAL_OR, LOGICAL_NOT,
	ATTRIBUTION,
	
	BITWISE_AND, BITWISE_OR, BITWISE_NOT, BITWISE_SL, BITWISE_SR, POINTER_REF,
	
	OPENING_PAREN, CLOSING_PAREN,
	OPENING_BRACE, CLOSING_BRACE,
	OPENING_BRACKET, CLOSING_BRACKET,
	
	SEMICOLON, COMMA
} _ATOM; // internal token representation
_ATOM tok;

typedef enum {
	DELIMITER = 1,
	CHAR_CONST, STRING_CONST, INTEGER_CONST, FLOAT_CONST, DOUBLE_CONST,
	IDENTIFIER, RESERVED, END
} _ATOM_TYPE;
_ATOM_TYPE token_type;

typedef enum {
	JF_NULL, 
	JF_BREAK, 
	JF_CONTINUE, 
	JF_RETURN
} _JUMP_FLAG;

typedef union {
	char c;
	short int si;
	int i;
	long int li;
	long long int lli;
	float f;
	double d;
	long double ld;
	void *p;
} _VALUE;

// basic data types
typedef enum {
	DT_VOID = 1, DT_CHAR, DT_INT, DT_FLOAT, DT_DOUBLE, DT_STRUCT
} _BASIC_DATA;

typedef enum {
	mSIGNED = 1, mUNSIGNED, mSHORT, mLONG
} _MODIFIER;

typedef struct {
	_BASIC_DATA type;
	_MODIFIER smodf, lmodf, modf3;
	_VALUE value;
	int ind_level; // holds the pointer indirection level
} _DATA;

typedef struct {
	char *str;
	_DATA data;
} _CONST;

typedef struct {
	char struct_name[ID_LEN + 1];
	int total_mem;
	struct{
		char field_name[ID_LEN + 1];
		_BASIC_DATA type;
		int ind_level;
		int dims[MAX_MATRIX_DIMS + 1];
		int offset;
		void *field_struct_pointer; // in case this member is a struct itself, this is a pointer to the details about the struct
	} fields[MAX_STRUCT_MEMBERS + 1];
} _STRUCT;
_STRUCT struct_table[STRUCT_TABLE_SIZE + 1];

// this is a special struct definition. it is used as a return from the function that calculates offsets of matrices or structs.
typedef struct{
	_BASIC_DATA datatype;
	int ind_level; 			// this is necessary, because _BASIC_DATA does not include pointers. so in case the variable is a pointer, this field will indicate it.
	int offset;				// the position of the variable inside a matrix or struct.
} _DATA_TYPE_OFFSET;

typedef struct {
	char var_name[ID_LEN];
	_DATA data; // holds the type of data and the value itself
	int dims[MAX_MATRIX_DIMS + 1];
	char constant;
	_STRUCT *structptr; // in case this variable is a struct
} _VAR;
_VAR global_variables[MAX_GLOBAL_VARS], local_variables[MAX_LOCAL_VARS];

typedef struct {
	char func_name[ID_LEN];
	_BASIC_DATA return_type;
	char *code_location;
	struct{ // holds information about each of the defined parameters, for later use when the function is called
		char param_name[ID_LEN];
		_BASIC_DATA type;
		char constant;
		int ind_level; // if the parameter is a pointer
	} parameters[MAX_USER_FUNC_PARAMS + 1]; // one extra to mark the end of the list
} _USER_FUNC;
_USER_FUNC user_func_table[MAX_USER_FUNC];

struct _keyword_table{
	char *keyword;
	_ATOM key;
} keyword_table[] = {
	"void", VOID,
	"char", CHAR,
	"int", INT,
	"float", FLOAT,
	"double", DOUBLE,
	"const", CONST,
	"enum", ENUM,
	"struct", STRUCT,
	"sizeof", SIZEOF,
	"return", RETURN,
	"if", IF,
	"then", THEN,
	"else", ELSE,
	"for", FOR,
	"do", DO,
	"break", BREAK,
	"continue", CONTINUE,
	"while", WHILE,
	"switch", SWITCH,
	"case", CASE,
	"default", DEFAULT,
	"include", INCLUDE,
	"", 0
};

typedef enum {
	SYNTAX,
	OPENING_PAREN_EXPECTED, 
	CLOSING_PAREN_EXPECTED,
	OPENING_BRACE_EXPECTED,
	CLOSING_BRACE_EXPECTED,
	OPENING_BRACKET_EXPECTED,
	CLOSING_BRACKET_EXPECTED,
	COMMA_EXPECTED,
	SEMICOLON_EXPECTED,
	VAR_TYPE_EXPECTED,
	IDENTIFIER_EXPECTED,
	EXCEEDED_GLOBAL_VAR_LIMIT,
	EXCEEDED_FUNC_DECL_LIMIT,
	NOT_VAR_OR_FUNC_OUTSIDE,
	NO_MAIN_FOUND,
	UNDECLARED_FUNC,
	SINGLE_QUOTE_EXPECTED,
	DOUBLE_QUOTE_EXPECTED,
	UNDECLARED_VARIABLE,
	MAX_PARAMS_LIMIT_REACHED,
	USER_FUNC_CALLS_LIMIT_REACHED,
	LOCAL_VAR_LIMIT_REACHED,
	RETURNING_VALUE_FROM_VOID_FUNCTION,
	INVALID_EXPRESSION,
	INVALID_ARGUMENT_FOR_BITWISE_NOT,
	WHILE_KEYWORD_EXPECTED,
	DUPLICATE_GLOBAL_VARIABLE,
	DUPLICATE_LOCAL_VARIABLE,
	STRING_CONSTANT_EXPECTED,
	POINTER_EXPECTED,
	INSUFFICIENT_ARGUMENTS,
	POINTER_SYNTAX,
	TOO_MANY_MATRIX_DIMENSIONS,
	INVALID_MATRIX_DIMENSION,
	MEMORY_ALLOCATION_FAILURE,
	MATRIX_INDEX_OUTSIDE_BOUNDS,
	INVALID_MATRIX_ATTRIBUTION,
	MATRIX_EXPECTED,
	UNKOWN_LIBRARY,
	UNKNOWN_DIRECTIVE,
	DIRECTIVE_SYNTAX,
	INCOMPATIBLE_FUNCTION_ARGUMENT,
	CONSTANT_VARIABLE_ASSIGNMENT,
	INVALID_BINARY_OPERANDS,
	STRUCT_EXPECTED,
	STRUCT_NAME_EXPECTED,
	UNDECLARED_STRUCT,
	INVALID_PRINTF_FORMAT_CODE
} _ERROR;

// variable declaration
char *error_table[] = {
	"syntax error",
	"syntax error: opening parenthesis expected",
	"syntax error: closing parenthesis expected",
	"syntax error: opening brace expected",
	"syntax error: closing brace expected",
	"syntax error: opening bracket expected",
	"syntax error: closing bracket expected",
	"syntax error: comma expected",
	"syntax error: semicolon expected",
	"syntax error: variable type expected in the declaration",
	"syntax error: identifier expected",
	"global variable limit reached (max = 100)",
	"function declaration limit reached (max = 100)",
	"syntax error: only variable and function declarations are allowed outside of functions",
	"main funtion not found",
	"undeclared function",
	"syntax error: single quote expected",
	"syntax error: double quotes expected",
	"undeclared variable or unkown constant",
	"maximum number of function paramters reached (max = 10)",
	"maximum number of program-defined function calls reached (max = 100)",
	"local variables limit reached (max = 200)",
	"returning value from void function",
	"invalid expression",
	"invalid argument for the bitwise not operation",
	"while part of do-while loop expected",
	"duplicate global variable declared",
	"duplicate local variable declared",
	"string constant expected",
	"pointer expected",
	"insufficient function arguments",
	"pointer syntax error",
	"declared matrix exceeds the maximum number of dimensions (max = 10)",
	"invalid matrix dimension",
	"memory allocation failure",
	"matrix index outside bounds",
	"invalid matrix attribution",
	"matrix expected",
	"unkown library",
	"unknown directive",
	"directive syntax error",
	"incompatible function argument",
	"constant variable assignment",
	"invalid binary operands",
	"struct expected",
	"struct name expected but token is not an identifier for any structs",
	"undeclared struct",
	"invalid printf format code"
};

// a table to hold library function entries that have been added by #include
struct{
	char func_name[ID_LEN + 1];
	_FPTR fp;
} active_func_table[ACTIVE_FUNC_TABLE_SIZE + 1];

struct{
	char str[ID_LEN + 1];
	_DATA data;
} active_const_table[ACTIVE_CONST_TABLE_SIZE + 1];

int active_func_table_tos;
int active_const_table_tos;

char *string_table[STRING_TABLE_SIZE];
int string_table_tos;

jmp_buf ebuf;

int struct_table_index;

int current_func_index;
int user_func_table_tos;
int user_func_call_index; // holds the current function call count (for use with the local stack history array
int local_var_tos_history[MAX_USER_FUNC_CALLS]; // holds the previous local variable stack upper bound;
int local_var_tos; // holds the current local variable stack upper bound;
int global_var_tos;

char token[CONST_LEN + 2]; // string token representation
char string_constant[CONST_LEN + 2]; // holds string and char constants without quotes and with escape sequences converted into the correct bytes
char *prog; // pointer to the current program position
char *pbuf; // pointer to the beginning of the source code
_DATA func_ret; // holds the value returned by the last function called

// ********** flags ***********
_JUMP_FLAG jump_flag;
// ********************

// functions
char isdelim(char c);
char is_idchar(char c);
int find_keyword(char *keyword);
_VAR *find_variable(char *var_name);
int find_local_var(char *var_name);
int find_global_var(char *var_name);
void local_push(_VAR *var);
void load_program(char *filename);
void free_mem(void);
void initial_setup(void);
void pre_scan(void);
void declare_func(void);
void declare_global(void);
void declare_local(void);
void declare_struct(void);
_STRUCT *find_struct(char *struct_name);
void get_token(void);
void show_error(_ERROR e);
void find_end_of_BLOCK(void);
void find_end_of_block(void);
void putback(void);

_FPTR find_lib_func(char *func_name);
_DATA get_var_value(char *var_name);
void assign_var(char *var_name, _DATA *data);
void incr_var(char *var_name);
void decr_var(char *var_name);
void *get_var_address(char *var_name);
char get_var_type(char *var_name);
char is_matrix(char *var_name);

_DATA_TYPE_OFFSET get_var_offset(char *var_name);

void eval(_DATA *v);
void eval_attrib(_DATA *v);
void eval_logical(_DATA *v);
void eval_relational(_DATA *v);
void eval_terms(_DATA *v);
void eval_factors(_DATA *v);
void eval_matrix(_DATA *v);
void eval_atom(_DATA *v);
void unary_minus(_DATA *v);
void unary_logical_not(_DATA *v);
void unary_bitwise_not(_DATA *v);
void unary_sizeof(_DATA *v);
void cast(_DATA *data, _ATOM type, int ind_level);

void interp_block(void);
int find_user_func(char *func_name);
void call_func(int func_index);
void call_lib_func(_FPTR fp);
void execute_main(void);
void get_func_parameters(int func_index);
void operate(_DATA *v1, _DATA *v2, _ATOM op);
void promote(_DATA *v1, _DATA *v2);
void convert_constant(void);
void convert_data(_DATA *data_to_convert, _BASIC_DATA into_type);
char is_pointer(_DATA *data);
char get_ind_level(char *var_name);
_VAR *get_var_pointer(char *var_name);
char get_data_size(_DATA *data);
int get_data_size2(_BASIC_DATA type);
char matrix_dim_count(_VAR *var);
int get_matrix_offset(char dim, _VAR *matrix);
char _is_matrix(_VAR *var);
char *add_string(void);
void free_string_table(void);

void exec_if(void);
void exec_for(void);
void exec_do(void);
void exec_while(void);
void exec_switch(void);
void exec_break(void);
void exec_continue(void);

void include_lib(char *lib_name);
_STRUCT *get_struct_table_pointer(char *struct_name);
_DATA get_constant(char *str);
