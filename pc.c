#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <conio.h>
#include <math.h>
#include <time.h>
#include "def.h"
#include "lib.h"

int main(int argc, char *argv[]){
	if(argc > 1) load_program(argv[1]);	
	else load_program("p.pc");

	prog = pbuf; // resets pointer to the beginning of the program

	initial_setup();
// pre scans the source
	pre_scan();

// finds the main function and executes it
	execute_main();


	/*
	register int i,j;
	for(i=0; i<struct_table_index; i++){
		puts(struct_table[i].struct_name);
		for(j=0; *struct_table[i].fields[j].field_name; j++){
			printf("\t%s - type: %d", struct_table[i].fields[j].field_name, struct_table[i].fields[j].type);
		}
		putchar('\n');
	}
	*/

// releases used memory
	free_mem();

	return 0;
}

void initial_setup(void){
	// sets up the stack variables
	global_var_tos = 0;
	local_var_tos = 0;
	user_func_call_index = 1;
	local_var_tos_history[0] = 0;
}

void free_mem(void){
	register int i;
	free_string_table();
	free(pbuf);
	
	for(i = 0; i < local_var_tos; i++)
		if(_is_matrix(&local_variables[i])) free(local_variables[i].data.value.p);
}

void load_program(char *filename){
	FILE *fp;
	int i;
	
	if((fp = fopen(filename, "rb")) == NULL){
		printf("program source file not found");
		exit(0);
	}
	
	if((pbuf = malloc(PROG_SIZE)) == NULL){
		printf("failed to allocate memory for the program source");
		exit(0);
	}
	
	prog = pbuf;
	i = 0;
	
	do{
		*prog = getc(fp);
		prog++;
		i++;
		
	} while(!feof(fp) && i < PROG_SIZE);
	
	fclose(fp);
	
	if(*(prog - 2) == 0x1A) *(prog - 2) = '\0';
	else *(prog - 1) = '\0';
}

void pre_scan(void){
	char *tp;
	
	do{
		tp = prog;
		get_token();
		if(token_type == END) return;
		
		if(tok == DIRECTIVE){
			get_token();
			if(tok != INCLUDE) show_error(UNKNOWN_DIRECTIVE);
			get_token();
			if(tok != LESS_THAN) show_error(DIRECTIVE_SYNTAX);
			get_token();
			include_lib(token);
			get_token();
			if(tok != GREATER_THAN) show_error(DIRECTIVE_SYNTAX);
			continue;
		}
		
		if(tok == STRUCT){
			declare_struct();
			continue;
		}
		
		if(tok == CONST) get_token();
		if(tok != VOID && tok != CHAR && tok != INT && tok != FLOAT && tok != DOUBLE) show_error(NOT_VAR_OR_FUNC_OUTSIDE);
		
		get_token();
		
		
		while(tok == ASTERISK) get_token();
		if(token_type != IDENTIFIER) show_error(IDENTIFIER_EXPECTED);

		get_token();
		if(tok == OPENING_PAREN){ //it must be a function declaration
			prog = tp;
			declare_func();
			find_end_of_BLOCK();
		}
		else { //it must be variable declarations
			prog = tp;
			declare_global();
		}
	} while(token_type != END);
	
}

void dbg(char *s){
	puts(s);
	system("pause");
}

void call_lib_func(_FPTR fp){
	func_ret.type = DT_INT;
	func_ret.value.i = 0;
	func_ret.ind_level = 0;
	
	fp();
}

_FPTR find_lib_func(char *func_name){
	register int i;
	
	for(i = 0; *active_func_table[i].func_name; i++)
		if(!strcmp(active_func_table[i].func_name, func_name)) return active_func_table[i].fp;
		
	return NULL;
}

_DATA get_constant(char *str){
	_DATA d;
	register int i;
	
	
	for(i = 0; i < active_const_table_tos; i++)
		if(!strcmp(active_const_table[i].str, str)){
			return active_const_table[i].data;
		}

	d.type = 0;
	return d;
}

void execute_main(void){
	register int i;

	for(i = 0; *user_func_table[i].func_name; i++)
		if(!strcmp(user_func_table[i].func_name, "main")){
			current_func_index = i;
			prog = user_func_table[i].code_location;
			interp_block(); // starts interpreting the main function block;
			return;
		}
	
	show_error(NO_MAIN_FOUND);
}

void call_func(int func_index){
	char *t;
	int temp_local_tos;
	
	if(user_func_call_index == MAX_USER_FUNC_CALLS) show_error(USER_FUNC_CALLS_LIMIT_REACHED);

	current_func_index = func_index;

	temp_local_tos = local_var_tos;
	get_func_parameters(func_index); // pushes the parameter variables into the local variables stack

	if(tok != CLOSING_PAREN) show_error(CLOSING_PAREN_EXPECTED);
	
	local_var_tos_history[user_func_call_index] = temp_local_tos;
	user_func_call_index++;

// saves the current program address
	t = prog;
	prog = user_func_table[func_index].code_location; // sets the program pointer to the beginning of the function code, just before the "{" token

// resets the function returning value to 0
	func_ret.value.c = 0;
	func_ret.value.i = 0;
	func_ret.value.f = 0.0;
	func_ret.value.d = 0.0;

// starts executing the function code
	interp_block();

// converts the expression value into the type defined by the function, if the function is not of the void type
	convert_data(&func_ret, user_func_table[func_index].return_type);
	
// recovers the previous program address
	prog = t;
	
// frees any arrays created by this function
	register int i;
	for(i = local_var_tos_history[user_func_call_index - 1]; i < local_var_tos; i++)
		if(_is_matrix(&local_variables[i])) free(local_variables[i].data.value.p);
	
//recovers the previous local variable top of stack
	user_func_call_index--;
	local_var_tos = local_var_tos_history[user_func_call_index];

// resets the block jump flag
	jump_flag = JF_NULL;
}

void get_func_parameters(int func_index){
	register int i;
	_VAR var;

	// if this function has no parameters
	if(!*user_func_table[func_index].parameters[0].param_name){
		get_token();
		return;
	}

	for(i = 0; *user_func_table[func_index].parameters[i].param_name; i++){
		eval(&var.data);
		convert_data(&var.data, user_func_table[func_index].parameters[i].type);
		var.data.ind_level = user_func_table[func_index].parameters[i].ind_level;
		strcpy(var.var_name, user_func_table[func_index].parameters[i].param_name);
		var.constant = user_func_table[func_index].parameters[i].constant;
		var.dims[0] = 0;
		local_push(&var);
		if(tok != COMMA && tok != CLOSING_PAREN) show_error(SYNTAX);
	}
}

void convert_data(_DATA *data_to_convert, _BASIC_DATA into_type){
	//converts the return value into the type defined by the function returning value
	switch(into_type){
		case DT_CHAR:
			switch(data_to_convert -> type){
				case DT_CHAR:
					break;
				case DT_INT:
					data_to_convert -> value.c = (char) data_to_convert -> value.i;
					break;
				case DT_FLOAT:
					data_to_convert -> value.c = (char) data_to_convert -> value.f;
					break;
				case DT_DOUBLE:
					data_to_convert -> value.c = (char) data_to_convert -> value.d;
			}
			data_to_convert -> type = DT_CHAR;
			break;
		case DT_INT:
			switch(data_to_convert -> type){
				case DT_CHAR:
					data_to_convert -> value.i = (int) data_to_convert -> value.c;
					break;
				case DT_INT:
					break;
				case DT_FLOAT:
					data_to_convert -> value.i = (int) data_to_convert -> value.f;
					break;
				case DT_DOUBLE:
					data_to_convert -> value.i = (int) data_to_convert -> value.d;
			}
			data_to_convert -> type = DT_INT;
			break;
		case DT_FLOAT:
			switch(data_to_convert -> type){
				case DT_CHAR:
					data_to_convert -> value.f = (float) data_to_convert -> value.c;
					break;
				case DT_INT:
					data_to_convert -> value.f = (float) data_to_convert -> value.i;
					break;
				case DT_FLOAT:
					break;
				case DT_DOUBLE:
					data_to_convert -> value.f = (float) data_to_convert -> value.d;
			}
			data_to_convert -> type = DT_FLOAT;
			break;
		case DT_DOUBLE:
			switch(data_to_convert -> type){
				case DT_CHAR:
					data_to_convert -> value.d = (double) data_to_convert -> value.c;
					break;
				case DT_INT:
					data_to_convert -> value.d = (double) data_to_convert -> value.i;
					break;
				case DT_FLOAT:
					data_to_convert -> value.d = (double) data_to_convert -> value.f;
					break;
				case DT_DOUBLE:
					break;
			}
			data_to_convert -> type = DT_DOUBLE;
	}
}

void interp_block(){
	_DATA expr;
	int brace = 0;
	
	do{
		// bypasses this block, if a break, continue or return statement has been found
		if(jump_flag){
			while(brace){
				if(*prog == '{') brace++;
				else if(*prog == '}') brace--;
				prog++;
			}
			break; // a break statement has been found somewhere
		}

		get_token();
		switch(tok){
			case RETURN:
				// evaluates the expression and puts it into the function return value variable
				get_token();
				if(tok == SEMICOLON){
					expr.type = DT_CHAR;
					expr.value.c = 0;
				}
				else{
					putback();
					eval(&expr);
				}
				if(tok != SEMICOLON) show_error(SEMICOLON_EXPECTED);
				func_ret = expr;
				jump_flag = JF_RETURN;
				break;
			case IF:
				exec_if();
				break;
			case FOR:
				exec_for();
				break;
			case WHILE:
				exec_while();
				break;
			case DO:
				exec_do();
				break;
			case BREAK:
				get_token();
				if(tok != SEMICOLON) show_error(SEMICOLON_EXPECTED);
				jump_flag = JF_BREAK;
				break;
			case CONTINUE:
				get_token();
				if(tok != SEMICOLON) show_error(SEMICOLON_EXPECTED);
				jump_flag = JF_CONTINUE;
				break;
			case CONST:
			case VOID:
			case CHAR:
			case INT:
			case FLOAT:
			case DOUBLE:
			case STRUCT:
				putback();
				declare_local();
				break;
			case OPENING_BRACE:
				brace++;
				break;
			case CLOSING_BRACE:
				brace--;
				break;
			case SEMICOLON:
				break;
			default:
				if(token_type == END) show_error(CLOSING_BRACE_EXPECTED);
				putback();
				eval(&expr);
				if(tok != SEMICOLON) show_error(SEMICOLON_EXPECTED);
		}		
	} while(brace); // exits when it finds the last closing brace
}

void exec_switch(void){
	
}

void exec_while(void){
	_DATA cond;
	char *cond_loc;
	
	cond_loc = prog; // holds the start of the condition expression

	eval(&cond);
	putback(); // puts the last token back, which may be a "{" token or another command
	convert_data(&cond, DT_INT);

	while(cond.value.i){
		interp_block();
		if(jump_flag == JF_BREAK || jump_flag == JF_RETURN) break; // if a continue, break or return command has been found, then this loop stops
		else if(jump_flag == JF_CONTINUE) jump_flag = JF_NULL;

		prog = cond_loc;
		eval(&cond);
		putback(); // puts the last token back
		convert_data(&cond, DT_INT);
	}

	if(jump_flag == JF_NULL) find_end_of_block();
	else if(jump_flag == JF_BREAK) jump_flag = JF_NULL; // resets the jump flag
}

void exec_do(void){
	_DATA cond;
	char *block_loc;

	jump_flag = JF_NULL;
	
	block_loc = prog;

	do{
		interp_block();
		get_token();
		if(tok != WHILE) show_error(WHILE_KEYWORD_EXPECTED);
		
		if(jump_flag == JF_BREAK || jump_flag == JF_RETURN){
			find_end_of_block(); // gets past the conditional expression
			// resets the loop jumping flag to null, in case this loop is inside another loop
			// but if the jump flag is a return flag, then it's not reset, otherwise the block which called this loop
			// will not be skipped, and the function that called that block will not stop executing
			if(jump_flag == JF_BREAK) jump_flag = JF_NULL; 
			break;
		}
		else if(jump_flag == JF_CONTINUE){
			prog = block_loc;
			jump_flag = JF_NULL;
			continue;
		}
		
		eval(&cond);
		convert_data(&cond, DT_CHAR);
		if(cond.value.c) prog = block_loc;
	} while(cond.value.c);
}

void exec_for(void){
	_DATA expr, cond;
	char *cond_loc, *incr_loc, *block_begin;
	
	get_token();
	if(tok != OPENING_PAREN) show_error(OPENING_PAREN_EXPECTED);
	get_token();
	if(tok != SEMICOLON){
		putback();
		eval(&expr);
	}
	if(tok != SEMICOLON) show_error(SEMICOLON_EXPECTED);

	cond_loc = prog; // holds the condition code location
	
	// checks for an empty condition, which means always true
	get_token();
	if(tok != SEMICOLON){
		putback();
		eval(&cond);
		convert_data(&cond, DT_INT);
		if(tok != SEMICOLON) show_error(SEMICOLON_EXPECTED);
	}
	else{
		cond.type = DT_INT;
		cond.value.i = 1;
	}

	incr_loc = prog; // holds the incremement code location
	
	// gets past the increment expression, since it is not needed in the first loop
	int paren = 1;
	do{
		if(*prog == '(') paren++;
		else if(*prog == ')') paren--;
		prog++;
	} while(paren && *prog);
	if(!*prog) show_error(CLOSING_PAREN_EXPECTED);

	block_begin = prog; // holds the beginning of the for block

	for( ; cond.value.i; ){
		interp_block();
		if(jump_flag == JF_CONTINUE) jump_flag = JF_NULL; // resets the jump flag, if a continue statement has been found, so that the loop continues to run normally
		else if(jump_flag == JF_BREAK || jump_flag == JF_RETURN) break; // if either a break or a return command has been found, this stops the loop
		
		prog = incr_loc;
		// checks for an empty increment expression
		get_token();
		if(tok != CLOSING_PAREN){
			putback();
			eval(&expr);
		}
		prog = cond_loc;
		
		// checks for an empty conditional expression, which means always true. 
		// Since if the condition is empty, it has been checked before entering the loop, 
		// the truth value of 1 is still valid, so there's no need to reset the condition to true
		get_token();
		if(tok != SEMICOLON){
			putback();
			eval(&cond);
			convert_data(&cond, DT_INT);
		}
		prog = block_begin;
	}

// if no jump flag has been found, then finds the end of the block, since after every loop, the program pointer is reset to the beginning of the block
	if(jump_flag == JF_NULL) find_end_of_block();
	else if(jump_flag == JF_BREAK) jump_flag = JF_NULL; // resets the flag, in case a break command was found, but not if a return command was found
}

void exec_if(void){
	_DATA cond;

	eval(&cond);

	convert_data(&cond, DT_CHAR); // converts the conditional expression into a char

	if(cond.value.c){
		putback(); // puts the "{" or any other token back
		interp_block();
		// finds the end of the if structure
		get_token();
		while(tok == ELSE){
			find_end_of_block(); // if there is an else token following the if statement, it must be skipped	
			get_token();
		}
		putback();
	}
	else{
		putback(); // puts the "{" or ";" or other tokens back
		find_end_of_block();
		get_token();
		if(tok == ELSE) interp_block();
		else putback();
	}
}

void find_end_of_block(void){
	int paren = 0;

	get_token();
	switch(tok){
		case IF:
			// skips the conditional expression between parenthesis
			get_token();
			if(tok != OPENING_PAREN) show_error(OPENING_PAREN_EXPECTED);
			paren = 1; // found the first parenthesis
			do{
				if(*prog == '(') paren++;
				else if(*prog == ')') paren--;
				prog++;
			} while(paren && *prog);
			if(!*prog) show_error(CLOSING_PAREN_EXPECTED);

			find_end_of_block();
			get_token();
			if(tok == ELSE) find_end_of_block();
			else
				putback();
			break;
		case OPENING_BRACE: // if it's a block, then the block is skipped
			putback();
			find_end_of_BLOCK();
			break;
		case FOR:
			get_token();
			if(tok != OPENING_PAREN) show_error(OPENING_PAREN_EXPECTED);
			paren = 1;
			do{
				if(*prog == '(') paren++;
				else if(*prog == ')') paren--;
				prog++;
			} while(paren && *prog);
			if(!*prog) show_error(CLOSING_PAREN_EXPECTED);
			get_token();
			if(tok != SEMICOLON){
				putback();
				find_end_of_block();
			}
			break;
			
		default: // if it's not a keyword, then it must be an expression
			putback(); // puts the last token back, which might be a ";" token
			while(*prog++ != ';' && *prog);
			if(!*prog) show_error(SEMICOLON_EXPECTED);
	}
}

void find_end_of_BLOCK(void){
	int brace = 0;
	
	do{
		if(*prog == '{') brace++;
		else if(*prog == '}') brace--;
		prog++;
	} while(brace && *prog);

	if(brace && !*prog) show_error(CLOSING_BRACE_EXPECTED);
}

void eval(_DATA *v){
	eval_attrib(v);
}

// this function is necessary because any variables may be a matrix, or a struct. and there could be structs and matrices inside the original struct, so this function needs to go deep
// inside the structs and matrices in order to find the final field value, and then return it back to be read or assigned a new value.
_DATA_TYPE_OFFSET get_var_offset(char *var_name){
	
}

void eval_attrib(_DATA *v){
	_DATA address;
	char var_name[ID_LEN];
	char *temp_prog;

	temp_prog = prog;

	get_token();
	if(token_type == IDENTIFIER){
		strcpy(var_name, token);
		get_token();
		if(tok == ATTRIBUTION){
			if(is_matrix(var_name)) show_error(INVALID_MATRIX_ATTRIBUTION);
			eval_attrib(v);
			assign_var(var_name, v);
			return;
		}
		else if(tok == OPENING_BRACKET){ // tests a matrix attribution
			if(!is_matrix(var_name)) show_error(MATRIX_EXPECTED);
			do{
				while(*prog != ']' && *prog) prog++;
				if(!*prog) show_error(CLOSING_BRACKET_EXPECTED);
				prog++; // gets past the "]" token
				get_token();
			} while(tok == OPENING_BRACKET);
			if(tok == ATTRIBUTION){ // is a matrix attrib.
				_VAR *matrix;
				_DATA index, expr;
				int i; char data_size;
				void *matrix_p;
				
				prog = temp_prog;
				get_token(); // gets past the variable name

				matrix = get_var_pointer(var_name);
				matrix_p = matrix -> data.value.p; // sets matrix_p to the beginning of the matrix memory block
				v -> type = matrix -> data.type;
				v -> ind_level = matrix -> data.ind_level;
				data_size = get_data_size(&matrix -> data); // gets the matrix data size
				
				// gets the correct matrix offset position for the assignment
				get_token(); // gets past the first bracket
				for(i = 0; matrix -> dims[i] && tok == OPENING_BRACKET; i++){
					eval(&index);
					if(tok != CLOSING_BRACKET) show_error(CLOSING_BRACKET_EXPECTED);
					convert_data(&index, DT_INT);
					matrix_p = matrix_p + index.value.i * get_matrix_offset(i, matrix) * data_size;
					get_token();
				}
				
				if(i != matrix_dim_count(matrix)) show_error(INVALID_MATRIX_ATTRIBUTION);
				
				// prog now should be past the equal sign
				
				eval(&expr);
				if(is_pointer(&(matrix -> data))){
					if(!is_pointer(&expr)) show_error(POINTER_EXPECTED);
					*(void **)(matrix_p) = expr.value.p;
					v -> value.p = expr.value.p;
				}
				else{
					switch(matrix -> data.type){
						case DT_CHAR:
							convert_data(&expr, DT_CHAR);
							*(char *) matrix_p = expr.value.c;
							break;
						case DT_INT:
							convert_data(&expr, DT_INT);
							*(int *) matrix_p = expr.value.i;
							break;
						case DT_FLOAT:
							convert_data(&expr, DT_FLOAT);
							*(float *) matrix_p = expr.value.f;
							break;
						case DT_DOUBLE:
							convert_data(&expr, DT_DOUBLE);
							*(double *) matrix_p = expr.value.d;
					}
				}
				return;
			}
		}
	}
	else if(tok == ASTERISK){ // tests if this is a pointer assignment
		while(tok != SEMICOLON && token_type != END){
			get_token();
			if(tok == ATTRIBUTION){ // is an attribution statement
				prog = temp_prog; // goes back to the beginning of the expression
				get_token(); // gets past the first asterisk
				eval_atom(&address);
				if(!is_pointer(&address)) show_error(POINTER_EXPECTED);
				
				// after evaluating the address expression, the token will be a "="
				eval(v); // evaluates the value to be attributed to the address
				convert_data(v, address.type);
				if(address.ind_level == 1)
					switch(address.type){
						case DT_CHAR:
							convert_data(v, DT_CHAR);
							*(char *) address.value.p = v -> value.c;
							break;
						case DT_INT:
							convert_data(v, DT_INT);
							*(int *) address.value.p = v -> value.i;
							break;
						case DT_FLOAT:
							convert_data(v, DT_FLOAT);
							*(float *) address.value.p = v -> value.f;
							break;
						case DT_DOUBLE:
							convert_data(v, DT_DOUBLE);
							*(double *) address.value.p = v -> value.d;
					}
				else{
					if(!is_pointer(v)) show_error(POINTER_EXPECTED);
					*(void **) address.value.p = v -> value.p;
				}
				return;
			}
		}
	}
	
	prog = temp_prog;
	eval_logical(v);
}

void eval_logical(_DATA *v){
	_DATA partial;
	char temp_tok;

	eval_relational(v);
	while(tok == LOGICAL_AND || tok == LOGICAL_OR){
		temp_tok = tok;
		eval_relational(&partial);
		operate(v, &partial, temp_tok); // this operates on v, and sets its value to the result
	}
}

void eval_relational(_DATA *v){
	_DATA partial;
	char temp_tok;

	eval_terms(v);
	while(tok == EQUAL || tok == NOT_EQUAL || tok == LESS_THAN || tok == GREATER_THAN || tok == LESS_THAN_OR_EQUAL || tok == GREATER_THAN_OR_EQUAL){
		temp_tok = tok;
		eval_terms(&partial);
		operate(v, &partial, temp_tok); // this operates on v, and sets its value to the result
	}
}

void eval_terms(_DATA *v){
	_DATA partial;
	char temp_tok;
	
	eval_factors(v);
	while(tok == PLUS || tok == MINUS){
		temp_tok = tok;
		eval_factors(&partial);
		operate(v, &partial, temp_tok); // this operates on v, and sets its value to the result
	}
}

void eval_factors(_DATA *v){
	_DATA partial;
	char temp_tok;

	eval_atom(v);
	
	while(tok == ASTERISK || tok == DIVISION || tok == MOD){
		temp_tok = tok;
		eval_atom(&partial);
		operate(v, &partial, temp_tok); // this operates on v, and sets its value to the result
	}
}

void eval_matrix(_DATA *v){
	eval_atom(v);
}

void eval_atom(_DATA *v){
	char temp_name[ID_LEN]; // holds the name of the variable or fuction found
	int func_index;

	get_token();
	if(tok == MINUS){
		eval_atom(v);
		unary_minus(v);
		putback();
	}
	else if(tok == LOGICAL_NOT){
		eval_atom(v);
		unary_logical_not(v);
		putback();
	}
	else if(tok == BITWISE_NOT){
		eval_atom(v);
		unary_bitwise_not(v);
		putback();
	}
	else if(tok == POINTER_REF){
		get_token();
		if(token_type != IDENTIFIER) show_error(IDENTIFIER_EXPECTED);
		v -> type = get_var_type(token);
		v -> ind_level = get_ind_level(token) + 1;
		v -> value.p = get_var_address(token);		
	}
	else if(tok == ASTERISK){ // starts pointer dereferencing
		eval_atom(v);
		if(!is_pointer(v)) show_error(POINTER_EXPECTED);
		
		if(v -> ind_level == 1)
			switch(v -> type){
				case DT_CHAR:
					v -> value.c = *(char *) v -> value.p;
					break;
				case DT_INT:
					v -> value.i = *(int *) v -> value.p;
					break;
				case DT_FLOAT:
					v -> value.f = *(float *) v -> value.p;
					break;
				case DT_DOUBLE:
					v -> value.d = *(double *) v -> value.p;
			}
		else v -> value.p = *(void **) v -> value.p;
		v -> ind_level--;
		putback();
	}
	else if(tok == SIZEOF){
		_ATOM temp_tok;
		
		get_token();
		if(tok != OPENING_PAREN) show_error(OPENING_PAREN_EXPECTED);
		get_token();
		temp_tok = tok;
		if(tok == VOID || tok == CHAR || tok == INT || tok == FLOAT || tok == DOUBLE){
			get_token();
			if(tok == ASTERISK){
				while(tok == ASTERISK) get_token();
				v -> value.i = sizeof(void*);
			}
			else{
				if(tok != CLOSING_PAREN) show_error(CLOSING_PAREN_EXPECTED);
				switch(temp_tok){
					case CHAR:
						v -> value.i = sizeof(char);
						break;
					case INT:
						v -> value.i = sizeof(int);
						break;
					case FLOAT:
						v -> value.i = sizeof(float);
						break;
					case DOUBLE:
						v -> value.i = sizeof(double);
				}
			}
		}
		else{
			putback();
			eval(v);
			unary_sizeof(v);
		}
		v -> type = DT_INT;
		v -> ind_level = 0;
	}
	else if(tok == OPENING_PAREN){
		_ATOM temptok;
		int ind_level;
		
		ind_level = 0;
		get_token();
		if(tok == VOID || tok == CHAR || tok == INT || tok == FLOAT || tok == DOUBLE){
			temptok = tok;
			get_token();
			while(tok == ASTERISK){
				ind_level++;
				get_token();
			}
			if(tok != CLOSING_PAREN) show_error(CLOSING_PAREN_EXPECTED);
			eval_atom(v);
			cast(v, temptok, ind_level);
			putback();
		}
		else{
			putback();
			eval(v);
			if(tok != CLOSING_PAREN) show_error(CLOSING_PAREN_EXPECTED);
		}
	}
	else if(token_type == CHAR_CONST){
		v -> ind_level = 0;
		v -> type = DT_CHAR;
		v -> value.c = *string_constant;
	}
	else if(token_type == STRING_CONST){
		v -> ind_level = 1;
		v -> type = DT_CHAR;
		v -> value.p = add_string();
	}
	else if(token_type == INTEGER_CONST){
		v -> ind_level = 0;
		v -> type = DT_INT;
		v -> value.i = atoi(token);
	}
	else if(token_type == FLOAT_CONST){
		v -> type = DT_FLOAT;
		v -> ind_level = 0;
		v -> value.f = atof(token);
	}
	else if(token_type == DOUBLE_CONST){
		v -> type = DT_DOUBLE;
		v -> ind_level = 0;
		v -> value.d = atof(token);
	}
	else if(token_type == IDENTIFIER){		
		strcpy(temp_name, token);
		get_token();
		if(tok == OPENING_BRACKET){ // matrix operations
			_VAR *matrix; // pointer to the matrix variable
			char i; char data_size; // matrix data size
			_DATA index;
			int dims;
			
			if(!is_matrix(temp_name)){ // if the variable is not a matrix, then it must be a pointer. gives the variable value and returns, so that eval_matrix can work on it.
				*v = get_var_value(temp_name);
				return;
			}
			// otherwise, it is a matrix
			matrix = get_var_pointer(temp_name); // gets a pointer to the variable holding the matrix address
			data_size = get_data_size(&matrix -> data);
			v -> value.p = matrix -> data.value.p; // sets v to the beginning of the memory block 
			v -> type = matrix -> data.type; 
			v -> ind_level = 1;
			
			dims = matrix_dim_count(matrix); // gets the number of dimensions for this matrix
			
			for(i = 0; i < dims; i++){
				eval(&index); // evaluates the index expression
				convert_data(&index, DT_INT);
				if(index.value.i < 0 || index.value.i >= matrix -> dims[i]) show_error(MATRIX_INDEX_OUTSIDE_BOUNDS);
				if(tok != CLOSING_BRACKET) show_error(CLOSING_BRACKET_EXPECTED);
							
				// if not evaluating the final dimension, it keeps returning pointers to the current position within the matrix
				if(i < dims - 1) v -> value.p = v -> value.p + ( index.value.i * get_matrix_offset(i, matrix) * data_size );
				get_token();
				if(tok != OPENING_BRACKET) break;
			}
			// if it has reached the last dimension, it gets the final value at that address, which is one of the basic data types
			if(i == dims - 1){
				v -> ind_level = 0;
				switch(matrix -> data.type){
					case DT_CHAR:
						v -> value.c = *( (char *)(v -> value.p) + index.value.i );
						break;
					case DT_INT:
						v -> value.i = *( (int *)(v -> value.p) + index.value.i );
						break;
					case DT_FLOAT:
						v -> value.f = *( (float *)(v -> value.p) + index.value.i );
						break;
					case DT_DOUBLE:
						v -> value.d = *( (double *)(v -> value.p) + index.value.i );
				}
			}
			putback(); // puts back the ";" token
		}
		else if(tok == STRUCT_DOT){
			_STRUCT *structptr;
			_VAR *varptr;
			
			if((varptr = find_variable(temp_name)) == NULL) show_error(UNDECLARED_VARIABLE);
			if(varptr -> structptr == NULL) show_error(STRUCT_EXPECTED);
		}
		else if(tok == INCREMENT){
			incr_var(temp_name);
		}
		else if(tok == DECREMENT){
			decr_var(temp_name);
		}
		else if(tok == OPENING_PAREN){ // function call			
			func_index = find_user_func(temp_name);
			if(func_index != -1){
				call_func(func_index);
			}
			else{
				_FPTR fp; // function pointer
				
				if((fp = find_lib_func(temp_name)) == NULL) show_error(UNDECLARED_FUNC);
				call_lib_func(fp);
			}
			*v = func_ret; // sets the value of this expression to the value returned by the function call
		}
		else{
			_DATA d;
			
			d = get_constant(temp_name);
			if(d.type != 0) *v = d;
			else *v = get_var_value(temp_name);

			putback();
		}
	}
	else
		show_error(INVALID_EXPRESSION);

	get_token(); // gets the next token (it must be a delimiter)
}

void cast(_DATA *data, _ATOM type, int ind_level){
	switch(type){ // converting into
		case CHAR:
			if(ind_level){ // converting into a pointer
				if(data -> ind_level);
					// no action
			}
			else{ // converting into non pointer
				if(data -> ind_level){
					//data -> value.c = (char) (int) data -> value.p;
				}
				else
					switch(data -> type){ // original type
						case DT_CHAR:
							break;
						case DT_INT:
							data -> value.c = (char) data -> value.i;
							break;
						case DT_FLOAT:
							data -> value.c = (char) data -> value.f;
							break;
						case DT_DOUBLE:
							data -> value.c = (char) data -> value.d;
					}
			}
			data -> type = DT_CHAR;
			break;
		case INT:
			if(ind_level){ // converting into a pointer
				if(data -> ind_level);
					// no action
			}
			else{ // converting into non pointer
				if(data -> ind_level){
					//data -> value.i = (int) data -> value.p;
				}
				else
					switch(data -> type){ // original type
						case DT_CHAR:
							data -> value.i = (int) data -> value.c;
							break;
						case DT_INT:
							break;
						case DT_FLOAT:
							data -> value.i = (int) data -> value.f;
							break;
						case DT_DOUBLE:
							data -> value.i = (int) data -> value.d;
					}
			}
			data -> type = DT_INT;
			break;
		case FLOAT:
			if(ind_level){ // converting into a pointer
				if(data -> ind_level);
					// no action
			}
			else{ // converting into non pointer
				if(data -> ind_level){
					//data -> value.f = (float) (int) data -> value.p;
				}
				else
					switch(data -> type){ // original type
						case DT_CHAR:
							data -> value.f = (float) data -> value.c;
							break;
						case DT_INT:
							data -> value.f = (float) data -> value.i;
							break;
						case DT_FLOAT:
							break;
						case DT_DOUBLE:
							data -> value.f = (float) data -> value.d;
					}
			}
			data -> type = DT_FLOAT;
			break;
		case DOUBLE:
			if(ind_level){ // converting into a pointer
				if(data -> ind_level);
					// no action
			}
			else{ // converting into non pointer
				if(data -> ind_level){
					//data -> value.d = (double) (int) data -> value.p;
				}
				else
					switch(data -> type){ // original type
						case DT_CHAR:
							data -> value.d = (double) data -> value.c;
							break;
						case DT_INT:
							data -> value.d = (double) data -> value.i;
							break;
						case DT_FLOAT:
							data -> value.d = (double) data -> value.f;
							break;
						case DT_DOUBLE:
							break;
							
					}
			}
			data -> type = DT_DOUBLE;
		
	}
}

int get_matrix_offset(char dim, _VAR *matrix){
	int i;
	int offset = 1;
	
	for(i = dim + 1; i < matrix_dim_count(matrix); i++)
		offset = offset * matrix -> dims[i];
	
	return offset;
}

char matrix_dim_count(_VAR *var){
	int i;
	
	for(i = 0; var -> dims[i]; i++);
	
	return i;
}

char get_data_size(_DATA *data){
	if(data -> ind_level > 0) return sizeof(void *);
	else
		switch(data -> type){
			case DT_CHAR:
				return sizeof(char);
			case DT_INT:
				return sizeof(int);
			case DT_FLOAT:
				return sizeof(float);
			case DT_DOUBLE:
				return sizeof(double);
		}
}

_VAR *get_var_pointer(char *var_name){
	register int i;

	for(i = local_var_tos_history[user_func_call_index - 1]; i < local_var_tos; i++)
		if(!strcmp(local_variables[i].var_name, var_name))
			return &local_variables[i];

	for(i = 0; i < global_var_tos; i++)
		if(!strcmp(global_variables[i].var_name, var_name)) 
			return &global_variables[i];

	show_error(UNDECLARED_VARIABLE);
}

char is_matrix(char *var_name){
	register int i;

	for(i = local_var_tos_history[user_func_call_index - 1]; i < local_var_tos; i++)
		if(!strcmp(local_variables[i].var_name, var_name))
			return local_variables[i].dims[0];

	for(i = 0; i < global_var_tos; i++)
		if(!strcmp(global_variables[i].var_name, var_name)) 
			return global_variables[i].dims[0];

	show_error(UNDECLARED_VARIABLE);
}

char _is_matrix(_VAR *var){
	return var -> dims[0];
}

char get_ind_level(char *var_name){
	register int i;
	
	for(i = local_var_tos_history[user_func_call_index - 1]; i < local_var_tos; i++)
		if(!strcmp(local_variables[i].var_name, var_name))
			return local_variables[i].data.ind_level;

	for(i = 0; i < global_var_tos; i++)
		if(!strcmp(global_variables[i].var_name, var_name)) 
			return global_variables[i].data.ind_level;

	show_error(UNDECLARED_VARIABLE);
}

char get_var_type(char *var_name){
	register int i;

	for(i = local_var_tos_history[user_func_call_index - 1]; i < local_var_tos; i++)
		if(!strcmp(local_variables[i].var_name, var_name)) 
			return local_variables[i].data.type;

	for(i = 0; i < global_var_tos; i++)
		if(!strcmp(global_variables[i].var_name, var_name)) 
			return global_variables[i].data.type;

	show_error(UNDECLARED_VARIABLE);
}

char is_pointer(_DATA *data){
	if(data -> ind_level > 0)
		return 1;
	else 
		return 0;	
}

void *get_var_address(char *var_name){
	register int i;

	for(i = local_var_tos_history[user_func_call_index - 1]; i < local_var_tos; i++)
		if(!strcmp(local_variables[i].var_name, var_name)) 
			return (void *) &local_variables[i].data.value;

	for(i = 0; i < global_var_tos; i++)
		if(!strcmp(global_variables[i].var_name, var_name)) 
			return (void *) &global_variables[i].data.value;

	show_error(UNDECLARED_VARIABLE);
}

void unary_sizeof(_DATA *v){
	switch(v -> type){
		case DT_CHAR:
			v -> value.i = sizeof(char);
			break;
		case DT_INT:
			v -> value.i = sizeof(int);
			break;
		case DT_FLOAT:
			v -> value.i = sizeof(float);
			break;
		case DT_DOUBLE:
			v -> value.i = sizeof(double);
	}
}

void unary_minus(_DATA *v){
	switch(v -> type){
		case DT_CHAR:
			v -> value.c = -(v -> value.c);
			break;
		case DT_INT:
			v -> value.i = -(v -> value.i);
			break;
		case DT_FLOAT:
			v -> value.f = -(v -> value.f);
			break;
		case DT_DOUBLE:
			v -> value.d = -(v -> value.d);
	}
}

void unary_logical_not(_DATA *v){
	switch(v -> type){
		case DT_CHAR:
			v -> value.i = !(v -> value.c);
			break;
		case DT_INT:
			v -> value.i = !(v -> value.i);
			break;
		case DT_FLOAT:
			v -> value.i = !(v -> value.f);
			break;
		case DT_DOUBLE:
			v -> value.i = !(v -> value.d);
	}
	v -> type = DT_INT;
}

void unary_bitwise_not(_DATA *v){
	switch(v -> type){
		case DT_CHAR:
			v -> value.c = ~(v -> value.c);
			break;
		case DT_INT:
			v -> value.i = ~(v -> value.i);
			break;
		case DT_FLOAT:
		case DT_DOUBLE:
			show_error(INVALID_ARGUMENT_FOR_BITWISE_NOT);
	}
}

void incr_var(char *var_name){
	register int i;

	for(i = local_var_tos_history[user_func_call_index - 1]; i < local_var_tos; i++)
		if(!strcmp(local_variables[i].var_name, var_name)){
			switch(local_variables[i].data.type){
				case DT_CHAR:
					local_variables[i].data.value.c++;
					break;
				case DT_INT:
					local_variables[i].data.value.i++;
					break;
				case DT_FLOAT:
					local_variables[i].data.value.f++;
					break;
				case DT_DOUBLE:
					local_variables[i].data.value.d++;
			}
			return;
		}

	for(i = 0; i < global_var_tos; i++)
		if(!strcmp(global_variables[i].var_name, var_name)){
			switch(global_variables[i].data.type){
				case DT_CHAR:
					global_variables[i].data.value.c++;
					break;
				case DT_INT:
					global_variables[i].data.value.i++;
					break;
				case DT_FLOAT:
					global_variables[i].data.value.f++;
					break;
				case DT_DOUBLE:
					global_variables[i].data.value.d++;
			}
			return;
		}
	
	show_error(UNDECLARED_VARIABLE);
}

void decr_var(char *var_name){
	register int i;

	for(i = local_var_tos_history[user_func_call_index - 1]; i < local_var_tos; i++)
		if(!strcmp(local_variables[i].var_name, var_name)){
			switch(local_variables[i].data.type){
				case DT_CHAR:
					local_variables[i].data.value.c--;
					break;
				case DT_INT:
					local_variables[i].data.value.i--;
					break;
				case DT_FLOAT:
					local_variables[i].data.value.f--;
					break;
				case DT_DOUBLE:
					local_variables[i].data.value.d--;
			}
			return;
		}
	
	for(i = 0; i < global_var_tos; i++)
		if(!strcmp(global_variables[i].var_name, var_name)){
			switch(global_variables[i].data.type){
				case DT_CHAR:
					global_variables[i].data.value.c--;
					break;
				case DT_INT:
					global_variables[i].data.value.i--;
					break;
				case DT_FLOAT:
					global_variables[i].data.value.f--;
					break;
				case DT_DOUBLE:
					global_variables[i].data.value.d--;
			}
			return;
		}
	
	show_error(UNDECLARED_VARIABLE);
}

// converts a literal string or char constant into constants with true escape sequences
void convert_constant(){
	char *s = string_constant;
	char *t = token;
	
	if(token_type == CHAR_CONST){
		t++;
		if(*t == '\\'){
			t++;
			switch(*t){
				case '0':
					*s++ = '\0';
					break;
				case 'a':
					*s++ = '\a';
					break;
				case 'b':
					*s++ = '\b';
					break;	
				case 'f':
					*s++ = '\f';
					break;
				case 'n':
					*s++ = '\n';
					break;
				case 'r':
					*s++ = '\r';
					break;
				case 't':
					*s++ = '\t';
					break;
				case 'v':
					*s++ = '\v';
					break;
				case '\\':
					*s++ = '\\';
					break;
				case '\'':
					*s++ = '\'';
					break;
				case '\"':
					*s++ = '\"';
			}
		}
		else{
			*s++ = *t;
		}
	}
	else if(token_type == STRING_CONST){
		t++;
		while(*t != '\"' && *t){
			if(*t == '\\'){
				t++;
				switch(*t){
					case '0':
						*s = '\0';
						break;
					case 'a':
						*s = '\a';
						break;
					case 'b':
						*s = '\b';
						break;	
					case 'f':
						*s = '\f';
						break;
					case 'n':
						*s = '\n';
						break;
					case 'r':
						*s = '\r';
						break;
					case 't':
						*s = '\t';
						break;
					case 'v':
						*s = '\v';
						break;
					case '\\':
						*s = '\\';
						break;
					case '\'':
						*s = '\'';
						break;
					case '\"':
						*s = '\"';
				}
			}
			else *s = *t;
			s++;
			t++;
		}
	}
	
	*s = '\0';
}

void assign_var(char *var_name, _DATA *v){
	register int i;

//check local variables first, from the previous stack index to the current one
	for(i = local_var_tos_history[user_func_call_index - 1]; i < local_var_tos; i++)
		if(!strcmp(local_variables[i].var_name, var_name)){
			if(local_variables[i].constant) show_error(CONSTANT_VARIABLE_ASSIGNMENT);
			convert_data(v, local_variables[i].data.type); // converts the expression contained in v to the type defined by the variable, before assigning
			local_variables[i].data = *v;
			return;
		}
		
	for(i = 0; i < global_var_tos; i++)
		if(!strcmp(global_variables[i].var_name, var_name)){
			if(global_variables[i].constant) show_error(CONSTANT_VARIABLE_ASSIGNMENT);
			convert_data(v, global_variables[i].data.type); // converts the expression contained in v to the type defined by the variable, before assigning
			global_variables[i].data = *v;
			return;
		}
	
	show_error(UNDECLARED_VARIABLE);
}

_DATA get_var_value(char *var_name){
	register int i;
	
	//check local variables first, from the previous stack index to the current one
	for(i = local_var_tos_history[user_func_call_index - 1]; i < local_var_tos; i++)
		if(!strcmp(local_variables[i].var_name, var_name)) return local_variables[i].data;
		
	for(i = 0; i < global_var_tos; i++)
		if(!strcmp(global_variables[i].var_name, var_name)) return global_variables[i].data;

	show_error(UNDECLARED_VARIABLE);
}

void declare_struct(void){
	int field_index, offset, ind_level, data_size;
	_BASIC_DATA dt;
	_STRUCT *sp; // in case any of the fields are structs, this is a pointer to the details about the struct for that field

	get_token();
	if(token_type != IDENTIFIER) show_error(IDENTIFIER_EXPECTED);
	strcpy(struct_table[struct_table_index].struct_name, token);
	get_token();
	
	if(tok != OPENING_BRACE) show_error(OPENING_BRACE_EXPECTED);
	
	get_token();
	field_index = 0;
	offset = 0;
	do{
		struct_table[struct_table_index].fields[field_index].offset = offset;
		switch(tok){
			case VOID:
				dt = DT_VOID;
				break;
			case CHAR:
				dt = DT_CHAR;
				break;
			case INT:
				dt = DT_INT;
				break;
			case FLOAT:
				dt = DT_FLOAT;
				break;
			case DOUBLE:
				dt = DT_DOUBLE;
				break;
			case STRUCT:
				dt = DT_STRUCT;
		}

		get_token();
		
		// checks if this is a struct type field
		if(dt == DT_STRUCT){
			if(token_type != IDENTIFIER) show_error(STRUCT_NAME_EXPECTED);
			if((sp = find_struct(token)) == NULL) show_error(UNDECLARED_STRUCT);
			struct_table[struct_table_index].fields[field_index].field_struct_pointer = sp;
			get_token();
		}
		
		ind_level = 0;
		while(tok == ASTERISK){
			ind_level++;
			get_token();
		}

		if(token_type != IDENTIFIER) show_error(IDENTIFIER_EXPECTED);
		strcpy(struct_table[struct_table_index].fields[field_index].field_name, token);

		if(ind_level) data_size = sizeof(void *);
		else if(dt == DT_STRUCT) data_size = sp -> total_mem; // data_size will be the size of the struct
		else data_size = get_data_size2(dt);
		get_token();

// **************** checks whether this is a matrix *******************************
		int i = 0;
		if(tok == OPENING_BRACKET){
			_DATA expr;
		
			while(tok == OPENING_BRACKET){
				if(i == MAX_MATRIX_DIMS) show_error(TOO_MANY_MATRIX_DIMENSIONS);
				eval(&expr);
				convert_data(&expr, DT_INT);
				if(expr.value.i <= 0) show_error(INVALID_MATRIX_DIMENSION);
				if(tok != CLOSING_BRACKET) show_error(CLOSING_BRACKET_EXPECTED);
				struct_table[struct_table_index].fields[field_index].dims[i] = expr.value.i;
				
				offset += expr.value.i * data_size;
				
				get_token();
				i++;
			}
		}
		struct_table[struct_table_index].fields[field_index].dims[i] = 0;
// ***********************************************************************************
		if(tok != SEMICOLON) show_error(SEMICOLON_EXPECTED);
		
		// if the offset was not set before (in case this field is a matrix), then it is set now
		if(!struct_table[struct_table_index].fields[field_index].dims[0]) offset += data_size;
		
		struct_table[struct_table_index].fields[field_index].type = dt;
		struct_table[struct_table_index].fields[field_index].ind_level = ind_level;
		field_index++;
		get_token();
	} while(tok != CLOSING_BRACE);
	
	get_token();
	if(tok != SEMICOLON) show_error(SEMICOLON_EXPECTED);
	
	struct_table[struct_table_index].total_mem = offset;
	struct_table_index++;
}

_STRUCT *find_struct(char *struct_name){
	register int i;

	for(i = 0; i < struct_table_index; i++)
		if(!strcmp(struct_table[i].struct_name, struct_name)) return &struct_table[i];
	
	return NULL;
}

void putback(void){
	char *t = token;

	while(*t++) prog--;
}

void declare_global(void){
	_BASIC_DATA dt;
	int ind_level;
	char constant = 0;

	get_token(); // gets past the data type
	if(tok == CONST){
		constant = 1;
		get_token();
	}
	
	switch(tok){
		case VOID:
			dt = DT_VOID;
			break;
		case CHAR:
			dt = DT_CHAR;
			break;
		case INT:
			dt = DT_INT;
			break;
		case FLOAT:
			dt = DT_FLOAT;
			break;
		case DOUBLE:
			dt = DT_DOUBLE;
	}

	do{
		if(global_var_tos == MAX_GLOBAL_VARS) show_error(EXCEEDED_GLOBAL_VAR_LIMIT);

		global_variables[global_var_tos].constant = constant;

		// initializes the variable to 0
		global_variables[global_var_tos].data.value.c = 0;
		global_variables[global_var_tos].data.value.i = 0;
		global_variables[global_var_tos].data.value.f = 0.0;
		global_variables[global_var_tos].data.value.d = 0.0;
		global_variables[global_var_tos].data.value.p = NULL;
		
		get_token();
// **************** checks whether this is a pointer declaration *******************************
		ind_level = 0;
		while(tok == ASTERISK){
			ind_level++;
			get_token();
		}
// *********************************************************************************************
		if(token_type != IDENTIFIER) show_error(IDENTIFIER_EXPECTED);
		
		// checks if there is another global variable with the same name
		if(find_global_var(token) != -1) show_error(DUPLICATE_GLOBAL_VARIABLE);
		
		global_variables[global_var_tos].data.type = dt;
		global_variables[global_var_tos].data.ind_level = ind_level;
		
		strcpy(global_variables[global_var_tos].var_name, token);
		get_token();

		// checks if this is a matrix declaration
		int i = 0;
		if(tok == OPENING_BRACKET){
			_DATA expr;
			int matrix_mem = 1;
			char data_size;
		
			data_size = get_data_size(&global_variables[global_var_tos].data);
			while(tok == OPENING_BRACKET){
				if(i == MAX_MATRIX_DIMS) show_error(TOO_MANY_MATRIX_DIMENSIONS);
				eval(&expr);
				convert_data(&expr, DT_INT);
				if(expr.value.i <= 0) show_error(INVALID_MATRIX_DIMENSION);
				if(tok != CLOSING_BRACKET) show_error(CLOSING_BRACKET_EXPECTED);
				global_variables[global_var_tos].dims[i] = expr.value.i;
				get_token();
				matrix_mem = matrix_mem * expr.value.i;
				i++;
			}
			if(tok != SEMICOLON) show_error(SEMICOLON_EXPECTED);			
			matrix_mem = matrix_mem * data_size;
			if((global_variables[global_var_tos].data.value.p = malloc(matrix_mem)) == NULL) show_error(MEMORY_ALLOCATION_FAILURE);
		}
		global_variables[global_var_tos].dims[i] = 0;
		// *****************************************************************************************************************


		// checks for variable initialization
		if(tok == ATTRIBUTION){
			eval(&global_variables[global_var_tos].data);
			// after the value has been assigned, the data could be of any type, hence it needs to be converted into the correct type for this variable
			convert_data(&global_variables[global_var_tos].data, dt);
		}
// the indirection level needs to be reset now, because if it is not its value might be changed to the attribution expression ind_level
		global_variables[global_var_tos].data.ind_level = ind_level;
		global_var_tos++;	
	} while(tok == COMMA);

	if(tok != SEMICOLON) show_error(SEMICOLON_EXPECTED);
}

_STRUCT *get_struct_table_pointer(char *struct_name){
	register int i;
	
	for(i = 0; *struct_table[i].struct_name; i++)
		if (!strcmp(struct_table[i].struct_name, struct_name)) return &struct_table[i];
	
	return NULL;
}

void declare_local(void){                        
	_BASIC_DATA dt;
	_VAR new_var;
	char ind_level;
	char constant = 0;
	_STRUCT *structptr;
	int data_size; // holds the data size in case variable is a matrix or a struct (or a matrix of structs)
	
	get_token(); // gets past the data type

	if(tok == CONST){
		constant = TRUE;
		get_token();
	}
	
	if(tok == STRUCT){
		get_token();
		if(token_type != IDENTIFIER) show_error(IDENTIFIER_EXPECTED);
		if((structptr = get_struct_table_pointer(token)) == NULL) show_error(UNDECLARED_STRUCT);
		dt = DT_STRUCT;
	}
	else
		switch(tok){
			case VOID:
				dt = DT_VOID;
				break;
			case CHAR:
				dt = DT_CHAR;
				break;
			case INT:
				dt = DT_INT;
				break;
			case FLOAT:
				dt = DT_FLOAT;
				break;
			case DOUBLE:
				dt = DT_DOUBLE;
		}

	do{
		if(local_var_tos == MAX_LOCAL_VARS) show_error(LOCAL_VAR_LIMIT_REACHED);

		new_var.constant = constant;

		// initializes the variable to 0
		new_var.data.value.c = 0;
		new_var.data.value.i = 0;
		new_var.data.value.f = 0.0;
		new_var.data.value.d = 0.0;
		new_var.data.value.p = NULL;

		// gets the variable name
		get_token();
// **************** checks whether this is a pointer declaration *******************************
		ind_level = 0;
		while(tok == ASTERISK){
			ind_level++;
			get_token();
		}		
// *********************************************************************************************
		if(token_type != IDENTIFIER) show_error(IDENTIFIER_EXPECTED);

		if(find_local_var(token) != -1) show_error(DUPLICATE_LOCAL_VARIABLE);

		new_var.data.type = dt;
		if(dt == DT_STRUCT) new_var.structptr = structptr;
		else new_var.structptr = NULL;
		new_var.data.ind_level = ind_level;
		strcpy(new_var.var_name, token);
		get_token();

		if(ind_level) data_size = sizeof(void *);
		else{
			if(dt == DT_STRUCT) data_size = structptr -> total_mem;
			else data_size = get_data_size(&new_var.data);
		}

		// checks if this is a matrix declaration
		int i = 0;
		if(tok == OPENING_BRACKET){
			_DATA expr;
			int matrix_mem = 1;
			
			while(tok == OPENING_BRACKET){
				if(i == MAX_MATRIX_DIMS) show_error(TOO_MANY_MATRIX_DIMENSIONS);
				eval(&expr);
				convert_data(&expr, DT_INT);
				if(expr.value.i <= 0) show_error(INVALID_MATRIX_DIMENSION);
				if(tok != CLOSING_BRACKET) show_error(CLOSING_BRACKET_EXPECTED);
				new_var.dims[i] = expr.value.i;
				get_token();
				matrix_mem = matrix_mem * expr.value.i;
				i++;
			}
			if(tok != SEMICOLON) show_error(SEMICOLON_EXPECTED);
			matrix_mem = matrix_mem * data_size;

			if((new_var.data.value.p = malloc(matrix_mem)) == NULL) show_error(MEMORY_ALLOCATION_FAILURE);
		}
		else if(dt == DT_STRUCT){ // if it's not a matrix, it might still be a 'struct' type of variable
			if((new_var.data.value.p = malloc(data_size)) == NULL) show_error(MEMORY_ALLOCATION_FAILURE);

			
		}
		new_var.dims[i] = 0; // sets the last variable dimention to 0, to mark the end of the list
		// *****************************************************************************************************************
		
		// in this step, the expression parser is called, and if there is a function call in the variable
		// initialization, this function will start from the current local TOS, and then recover it
		// after this, the local TOS will be the same as before the function call, and will be ready to receive the new variable
		if(tok == ATTRIBUTION){
			eval(&new_var.data);
			// after the value has been assigned, the data could be of any type, hence it needs to be converted into the correct type for this variable
			convert_data(&new_var.data, dt);
		}

		// the indirection level needs to be reset now, because if it not, its value might be changed to the expression ind_level		
		new_var.data.ind_level = ind_level;
		// assigns the new variable to the local stack
		local_variables[local_var_tos] = new_var;		
		local_var_tos++;
	} while(tok == COMMA);

	if(tok != SEMICOLON) show_error(SEMICOLON_EXPECTED);
}

void declare_func(void){
	_USER_FUNC *func; // variable to hold a pointer to the user function top of stack
	_BASIC_DATA param_data_type; // function data type
	char param_count; // number of parameters found in the declaration

	if(user_func_table_tos == MAX_USER_FUNC - 1) show_error(EXCEEDED_FUNC_DECL_LIMIT);

	func = &user_func_table[user_func_table_tos];

	get_token();

	switch(tok){
		case VOID:
			func -> return_type = DT_VOID;
			break;
		case CHAR:
			func -> return_type = DT_CHAR;
			break;
		case INT:
			func -> return_type = DT_INT;
			break;
		case FLOAT:
			func -> return_type = DT_FLOAT;
			break;
		case DOUBLE:
			func -> return_type = DT_DOUBLE;
		
	}

	get_token(); // gets the function name
	strcpy(func -> func_name, token);
	get_token(); // gets past "("

	param_count = 0;
	
	get_token();
	if(tok == CLOSING_PAREN || tok == VOID){
		func -> parameters[0].param_name[0] = '\0'; // assigns a null character to the name of the first parameter entry
		if(tok == VOID) get_token();
	}   
	else{
		putback();
		do{
			if(param_count == MAX_USER_FUNC_PARAMS) show_error(MAX_PARAMS_LIMIT_REACHED);
			get_token();
			if(tok == CONST){
				func -> parameters[param_count].constant = 1;
				get_token();
			}
			if(tok != VOID && tok != CHAR && tok != INT && tok != FLOAT && tok != DOUBLE) show_error(VAR_TYPE_EXPECTED);
			
			// gets the parameter type
			switch(tok){
				case CHAR:
					func -> parameters[param_count].type = DT_CHAR;
					break;
				case INT:
					func -> parameters[param_count].type = DT_INT;
					break;
				case FLOAT:
					func -> parameters[param_count].type = DT_FLOAT;
					break;
				case DOUBLE:
					func -> parameters[param_count].type = DT_DOUBLE;
			}
			
			get_token();
			while(tok == ASTERISK){
				func -> parameters[param_count].ind_level++;
				get_token();
			}
			if(token_type != IDENTIFIER) show_error(IDENTIFIER_EXPECTED);
			strcpy(func -> parameters[param_count].param_name, token);
				
			get_token();
			param_count++;
		} while(tok == COMMA);
	}
		
	if(tok != CLOSING_PAREN) show_error(CLOSING_PAREN_EXPECTED);

	func -> code_location = prog; // sets the function starting point to  just after the "(" token
	
	get_token(); // gets to the "{" token
	if(tok != OPENING_BRACE) show_error(OPENING_BRACE_EXPECTED);
	putback(); // puts the "{" back so that it can be found by find_end_of_BLOCK()

	*func -> parameters[param_count].param_name = '\0'; // marks the end of the parameter list with a null character
	user_func_table_tos++;
}

void show_error(_ERROR e){
	int line = 1;
	char *t = pbuf;

	puts(error_table[e]);
	
	while(t < prog){
		t++;
		if(*t == '\n') line++;
	}
	
	printf("line number: %d\n", line);
	printf("near: %s", token);
	system("pause");

	exit(0);
}

char *add_string(void){
	if(string_table_tos == STRING_TABLE_SIZE){
		free_string_table();
		string_table_tos = 0;
	}
	
	if( (string_table[string_table_tos] = malloc(strlen(string_constant) + 1)) == NULL ) show_error(MEMORY_ALLOCATION_FAILURE);

	strcpy(string_table[string_table_tos], string_constant);
	string_table_tos++;

	return string_table[string_table_tos - 1];
}

void free_string_table(void){
	register int i;
	
	for(i = string_table_tos - 1; i >= 0; i--)
		free(string_table[i]);
}

void get_token(void){
	char *t;
	// skip blank spaces

	*token = '\0';
	tok = 0;
	t = token;
	
	while(isspace(*prog) || *prog == '\n') prog++;

	if(*prog == '\0'){
		token_type = END;
		return;
	}

	if(*prog == '\''){
		*t++ = '\'';
		prog++;
		if(*prog == '\\'){
			*t++ = '\\';
			prog++;
			*t++ = *prog++;
		}
		else *t++ = *prog++;
		
		if(*prog != '\'') show_error(SINGLE_QUOTE_EXPECTED);
		
		*t++ = '\'';
		prog++;
		token_type = CHAR_CONST;
		*t = '\0';
		convert_constant(); // converts this string token with quotation marks to a non quotation marks string, and also converts escape sequences to their real bytes
	}
	else if(*prog == '\"'){
		*t++ = '\"';
		prog++;
		while(*prog != '\"' && *prog) *t++ = *prog++;
		if(*prog != '\"') show_error(DOUBLE_QUOTE_EXPECTED);
		*t++ = '\"';
		prog++;
		token_type = STRING_CONST;
		*t = '\0';
		convert_constant(); // converts this string token qith quotation marks to a non quotation marks string, and also converts escape sequences to their real bytes
	}
	else if(isdigit(*prog)){
		while(isdigit(*prog)) *t++ = *prog++;
		if(*prog == '.'){
			*t++ = '.';
			prog++;
			while(isdigit(*prog)) *t++ = *prog++;
			if(*prog == 'D'){
				prog++;
				token_type = DOUBLE_CONST;
			}
			else
				token_type = FLOAT_CONST;
		}
		else token_type = INTEGER_CONST;
	}
	else if(is_idchar(*prog)){
		while(is_idchar(*prog) || isdigit(*prog))
			*t++ = *prog++;
		*t = '\0';

		if((tok = find_keyword(token)) != -1) token_type = RESERVED;
		else token_type = IDENTIFIER;
	}
	else if(isdelim(*prog)){
		token_type = DELIMITER;	
		
		if(*prog == '#'){
			*t++ = *prog++;
			tok = DIRECTIVE;
		}
		else if(*prog == '['){
			*t++ = *prog++;
			tok = OPENING_BRACKET;
		}
		else if(*prog == ']'){
			*t++ = *prog++;
			tok = CLOSING_BRACKET;
		}
		else if(*prog == '{'){
			*t++ = *prog++;
			tok = OPENING_BRACE;
		}
		else if(*prog == '}'){
			*t++ = *prog++;
			tok = CLOSING_BRACE;
		}
		else if(*prog == '='){
			*t++ = *prog++;
			if (*prog == '='){
				*t++ = *prog++;
				tok = EQUAL;
			}
			else tok = ATTRIBUTION;
		}
		else if(*prog == '&'){
			*t++ = *prog++;
			if(*prog == '&'){
				*t++ = *prog++;
				tok = LOGICAL_AND;
			}
			else tok = POINTER_REF;
		}
		else if(*prog == '|'){
			*t++ = *prog++;
			if (*prog == '|'){
				*t++ = *prog++;
				tok = LOGICAL_OR;
			}
			else tok = BITWISE_OR;
		}
		else if(*prog == '~'){
			*t++ = *prog++;
			tok = BITWISE_NOT;
		}
		else if(*prog == '<'){
			*t++ = *prog++;
			if (*prog == '='){
				*t++ = *prog++;
				tok = LESS_THAN_OR_EQUAL;
			}
			else tok = LESS_THAN;
		}
		else if(*prog == '>'){
			*t++ = *prog++;
			if (*prog == '='){
				*t++ = *prog++;
				tok = GREATER_THAN_OR_EQUAL;
			}
			else tok = GREATER_THAN;
		}
		else if(*prog == '!'){
			*t++ = *prog++;
			if(*prog == '='){
				*t++ = *prog++;
				tok = NOT_EQUAL;
			}
			else tok = LOGICAL_NOT;
		}
		else if(*prog == '+'){
			*t++ = *prog++;
			if(*prog == '+'){
				*t++ = *prog++;
				tok = INCREMENT;
			}
			else tok = PLUS;
		}
		else if(*prog == '-'){
			*t++ = *prog++;
			if(*prog == '-'){
				*t++ = *prog++;
				tok = DECREMENT;
			}
			else if(*prog == '>'){
				*t++ = *prog++;
				tok = STRUCT_ARROW;
			}
			else tok = MINUS;
		}
		else if(*prog == '*'){
			*t++ = *prog++;
			tok = ASTERISK;
		}
		else if(*prog == '/'){
			*t++ = *prog++;
			tok = DIVISION;
		}
		else if(*prog == '%'){
			*t++ = *prog++;
			tok = MOD;
		}
		else if(*prog == '('){
			*t++ = *prog++;
			tok = OPENING_PAREN;
		}
		else if(*prog == ')'){
			*t++ = *prog++;
			tok = CLOSING_PAREN;
		}
		else if(*prog == ';'){
			*t++ = *prog++;
			tok = SEMICOLON;
		}
		else if(*prog == ','){
			*t++ = *prog++;
			tok = COMMA;
		}
		else if(*prog == '.'){
			*t++ = *prog++;
			tok = STRUCT_DOT;
		}
	}

	*t = '\0';
}

int find_user_func(char *func_name){
	register int i;
	
	for(i = 0; *user_func_table[i].func_name; i++)
		if(!strcmp(user_func_table[i].func_name, func_name))
			return i;
	
	return -1;
}

int find_local_var(char *var_name){
	register int i;
	
	for(i = local_var_tos_history[user_func_call_index - 1]; (i < local_var_tos) && (*local_variables[i].var_name); i++)
		if(!strcmp(local_variables[i].var_name, var_name)) return i;
	
	return -1;
}

int find_global_var(char *var_name){
	register int i;
	
	for(i = 0; (i < global_var_tos) && (*global_variables[i].var_name); i++)
		if(!strcmp(global_variables[i].var_name, var_name)) return i;
	
	return -1;
}

_VAR *find_variable(char *var_name){
	register int i;

	//check local variables first, from the previous stack index to the current one
	for(i = local_var_tos_history[user_func_call_index - 1]; (i < local_var_tos) && (*local_variables[i].var_name); i++)
		if(!strcmp(local_variables[i].var_name, var_name)) return &local_variables[i];

	for(i = 0; (i < global_var_tos) && (*global_variables[i].var_name); i++)
		if(!strcmp(global_variables[i].var_name, var_name)) return &global_variables[i];
	
	return NULL;
}

void local_push(_VAR *var){
	local_variables[local_var_tos] = *var;
	local_var_tos++;
}

int find_keyword(char *keyword){
	register int i;
	
	for(i = 0; keyword_table[i].key; i++)
		if (!strcmp(keyword_table[i].keyword, keyword)) return keyword_table[i].key;
	
	return -1;
}

int get_data_size2(_BASIC_DATA type){
	switch(type){
		case DT_CHAR:
			return sizeof(char);
		case DT_INT:
			return sizeof(int);
		case DT_FLOAT:
			return sizeof(float);
		case DT_DOUBLE:
			return sizeof(double);		
	}
}

int get_truth_value(_DATA *v){
	if(v -> ind_level){
		if(v -> value.p) return 1;
		else return 0;
	}

	switch(v -> type){
		case DT_CHAR:
			return (v -> value.c ? 1 : 0);
		case DT_INT:
			return (v -> value.i ? 1 : 0);
		case DT_FLOAT:
			return (v -> value.f ? 1 : 0);
		case DT_DOUBLE:
			return (v -> value.d ? 1 : 0);
	}
}

void promote(_DATA *v1, _DATA *v2){
	switch(v1 -> type){
		case DT_CHAR:
			switch(v2 -> type){
				case DT_CHAR:
					break;
				case DT_INT:
					v1 -> value.i = v1 -> value.c;
					v1 -> type = DT_INT;
					break;
				case DT_FLOAT:
					v1 -> value.f = v1 -> value.c;
					v1 -> type = DT_FLOAT;
					break;
				case DT_DOUBLE:
					v1 -> value.d = v1 -> value.c;
					v1 -> type = DT_DOUBLE;
			}
			break;
		case DT_INT:
			switch(v2 -> type){
				case DT_CHAR:
					v2 -> value.i = v2 -> value.c;
					v2 -> type = DT_INT;
					break;
				case DT_INT:
					break;
				case DT_FLOAT:
					v1 -> value.f = v1 -> value.i;
					v1 -> type = DT_FLOAT;
					break;
				case DT_DOUBLE:
					v1 -> value.d = v1 -> value.i;
					v1 -> type = DT_DOUBLE;
			}
			break;
		case DT_FLOAT:
			switch(v2 -> type){
				case DT_CHAR:
					v2 -> value.f = v2 -> value.c;
					v2 -> type = DT_FLOAT;
					break;
				case DT_INT:
					v2 -> value.f = v2 -> value.i;
					v2 -> type = DT_FLOAT;
					break;
				case DT_FLOAT:
					break;
				case DT_DOUBLE:
					v1 -> value.d = v1 -> value.f;
					v1 -> type = DT_DOUBLE;
			}
			break;
		case DT_DOUBLE:
			switch(v2 -> type){
				case DT_CHAR:
					v2 -> value.d = v2 -> value.c;
					v2 -> type = DT_DOUBLE;
					break;
				case DT_INT:
					v2 -> value.d = v2 -> value.i;
					v2 -> type = DT_DOUBLE;
					break;
				case DT_FLOAT:
					v2 -> value.d = v2 -> value.f;
					v2 -> type = DT_DOUBLE;
					break;
				case DT_DOUBLE:
					break;
			}
	}
}

void operate(_DATA *v1, _DATA *v2, _ATOM op){
	//promote(v1, v2); // converts both operands into the same type

	switch(op){
		case LOGICAL_AND:
			v1 -> value.i = get_truth_value(v1) && get_truth_value(v2);
			v1 -> type = DT_INT;
			v1 -> ind_level = 0;
			break;
		case LOGICAL_OR:
			v1 -> value.i = get_truth_value(v1) || get_truth_value(v2);
			v1 -> type = DT_INT;
			v1 -> ind_level = 0;
			break;
		case LESS_THAN:
			if(v1 -> ind_level && v2 -> ind_level)
				v1 -> value.i = v1 -> value.p < v2 -> value.p;
			else if(v1 -> ind_level || v2 -> ind_level)
				show_error(INVALID_BINARY_OPERANDS);
			else
				switch(v1 -> type){
					case DT_CHAR:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.c < v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.c < v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.c < v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.c < v2 -> value.d;
						}
						break;
					case DT_INT:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.i < v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.i < v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.i < v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.i < v2 -> value.d;
						}
						break;
					case DT_FLOAT:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.f < v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.f < v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.f < v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.f < v2 -> value.d;
						}
						break;
					case DT_DOUBLE:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.d < v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.d < v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.d < v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.d < v2 -> value.d;
						}
			}
			v1 -> type = DT_INT;
			v1 -> ind_level = 0;
			break;
		case LESS_THAN_OR_EQUAL:
			if(v1 -> ind_level && v2 -> ind_level)
				v1 -> value.i = v1 -> value.p <= v2 -> value.p;
			else if(v1 -> ind_level || v2 -> ind_level)
				show_error(INVALID_BINARY_OPERANDS);
			else
				switch(v1 -> type){
					case DT_CHAR:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.c <= v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.c <= v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.c <= v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.c <= v2 -> value.d;
						}
						break;
					case DT_INT:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.i <= v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.i <= v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.i <= v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.i <= v2 -> value.d;
						}
						break;
					case DT_FLOAT:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.f <= v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.f <= v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.f <= v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.f <= v2 -> value.d;
						}
						break;
					case DT_DOUBLE:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.d <= v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.d <= v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.d <= v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.d <= v2 -> value.d;
						}
			}
			v1 -> type = DT_INT;
			v1 -> ind_level = 0;
			break;
		case EQUAL:	
			if(v1 -> ind_level && v2 -> ind_level)
				v1 -> value.i = v1 -> value.p == v2 -> value.p;
			else if(v1 -> ind_level || v2 -> ind_level)
				show_error(INVALID_BINARY_OPERANDS);
			else
				switch(v1 -> type){
					case DT_CHAR:
						switch(v2 -> type){
							case DT_CHAR:
								
								v1 -> value.i = v1 -> value.c == v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.c == v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.c == v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.c == v2 -> value.d;
						}
						break;
					case DT_INT:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.i == v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.i == v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.i == v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.i == v2 -> value.d;
						}
						break;
					case DT_FLOAT:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.f == v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.f == v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.f == v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.f == v2 -> value.d;
						}
						break;
					case DT_DOUBLE:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.d == v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.d == v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.d == v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.d == v2 -> value.d;
						}
			}
			v1 -> type = DT_INT;
			v1 -> ind_level = 0;
			break;
		case GREATER_THAN:
			if(v1 -> ind_level && v2 -> ind_level)
				v1 -> value.i = v1 -> value.p > v2 -> value.p;
			else if(v1 -> ind_level || v2 -> ind_level)
				show_error(INVALID_BINARY_OPERANDS);
			else
				switch(v1 -> type){
					case DT_CHAR:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.c > v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.c > v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.c > v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.c > v2 -> value.d;
						}
						break;
					case DT_INT:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.i > v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.i > v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.i > v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.i > v2 -> value.d;
						}
						break;
					case DT_FLOAT:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.f > v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.f > v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.f > v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.f > v2 -> value.d;
						}
						break;
					case DT_DOUBLE:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.d > v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.d > v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.d > v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.d > v2 -> value.d;
						}
			}
			v1 -> type = DT_INT;
			v1 -> ind_level = 0;
			break;
		case GREATER_THAN_OR_EQUAL:
			if(v1 -> ind_level && v2 -> ind_level)
				v1 -> value.i = v1 -> value.p >= v2 -> value.p;
			else if(v1 -> ind_level || v2 -> ind_level)
				show_error(INVALID_BINARY_OPERANDS);
			else
				switch(v1 -> type){
					case DT_CHAR:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.c >= v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.c >= v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.c >= v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.c >= v2 -> value.d;
						}
						break;
					case DT_INT:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.i >= v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.i >= v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.i >= v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.i >= v2 -> value.d;
						}
						break;
					case DT_FLOAT:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.f >= v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.f >= v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.f >= v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.f >= v2 -> value.d;
						}
						break;
					case DT_DOUBLE:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.d >= v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.d >= v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.d >= v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.d >= v2 -> value.d;
						}
			}
			v1 -> type = DT_INT;
			v1 -> ind_level = 0;
			break;
		case NOT_EQUAL:
			if(v1 -> ind_level && v2 -> ind_level)
				v1 -> value.i = v1 -> value.p != v2 -> value.p;
			else if(v1 -> ind_level || v2 -> ind_level)
				show_error(INVALID_BINARY_OPERANDS);
			else
				switch(v1 -> type){
					case DT_CHAR:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.c != v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.c != v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.c != v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.c != v2 -> value.d;
						}
						break;
					case DT_INT:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.i != v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.i != v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.i != v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.i != v2 -> value.d;
						}
						break;
					case DT_FLOAT:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.f != v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.f != v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.f != v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.f != v2 -> value.d;
						}
						break;
					case DT_DOUBLE:
						switch(v2 -> type){
							case DT_CHAR:
								v1 -> value.i = v1 -> value.d != v2 -> value.c;
								break;
							case DT_INT:
								v1 -> value.i = v1 -> value.d != v2 -> value.i;
								break;
							case DT_FLOAT:
								v1 -> value.i = v1 -> value.d != v2 -> value.f;
								break;
							case DT_DOUBLE:
								v1 -> value.i = v1 -> value.d != v2 -> value.d;
						}
			}
			v1 -> type = DT_INT;
			v1 -> ind_level = 0;
			break;
		case PLUS:
			if(v1 -> ind_level && v2 -> ind_level) show_error(INVALID_BINARY_OPERANDS);
			switch(v1 -> type){
				case DT_CHAR:
					switch(v2 -> type){
						case DT_CHAR:
							if(v1 -> ind_level)
								v1 -> value.p = (char *)(v1 -> value.p) + v2 -> value.c;
							else if(v2 -> ind_level){
								v1 -> value.p = v1 -> value.c + (char *)(v2 -> value.p);
								v1 -> ind_level = v2 -> ind_level;
							}
							else
								v1 -> value.c = v1 -> value.c + v2 -> value.c;
							break;
						case DT_INT:
							if(v1 -> ind_level)
								v1 -> value.p = (char *)(v1 -> value.p) + v2 -> value.i;
							else if(v2 -> ind_level){
								v1 -> value.p = v1 -> value.c + (int *)(v2 -> value.p);
								v1 -> ind_level = v2 -> ind_level;
								v1 -> type = DT_INT;
							}
							else{
								v1 -> value.i = v1 -> value.c + v2 -> value.i;
								v1 -> type = DT_INT;
							}
							break;
						case DT_FLOAT:
							if(v1 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else if(v2 -> ind_level){
								v1 -> value.p = v1 -> value.c + (float *)(v2 -> value.p);
								v1 -> ind_level = v2 -> ind_level;
								v1 -> type = DT_FLOAT;
							}
							else{
								v1 -> value.f = v1 -> value.c + v2 -> value.f;
								v1 -> type = DT_FLOAT;
							}
							break;
						case DT_DOUBLE:
							if(v1 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else if(v2 -> ind_level){
								v1 -> value.p = v1 -> value.c + (double *)(v2 -> value.p);
								v1 -> ind_level = v2 -> ind_level;
								v1 -> type = DT_DOUBLE;
							}
							else{
								v1 -> value.d = v1 -> value.c + v2 -> value.d;
								v1 -> type = DT_DOUBLE;
							}
					}
					break;
				case DT_INT:
					switch(v2 -> type){
						case DT_CHAR:
							if(v1 -> ind_level)
								v1 -> value.p = (int *)(v1 -> value.p) + v2 -> value.c;
							else if(v2 -> ind_level){
								v1 -> value.p = v1 -> value.i + (char *)(v2 -> value.p);
								v1 -> ind_level = v2 -> ind_level;
							}
							else
								v1 -> value.i = v1 -> value.i + v2 -> value.c;
							break;
						case DT_INT:
							if(v1 -> ind_level)
								v1 -> value.p = (int *)(v1 -> value.p) + v2 -> value.i;
							else if(v2 -> ind_level){
								v1 -> value.p = v1 -> value.i + (int *)(v2 -> value.p);
								v1 -> ind_level = v2 -> ind_level;
								v1 -> type = DT_INT;
							}
							else v1 -> value.i = v1 -> value.i + v2 -> value.i;
							break;
						case DT_FLOAT:
							if(v1 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else if(v2 -> ind_level){
								v1 -> value.p = v1 -> value.i + (float *)(v2 -> value.p);
								v1 -> ind_level = v2 -> ind_level;
								v1 -> type = DT_FLOAT;
							}
							else{
								v1 -> value.f = v1 -> value.i + v2 -> value.f;
								v1 -> type = DT_FLOAT;
							}
							break;
						case DT_DOUBLE:
							if(v1 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else if(v2 -> ind_level){
								v1 -> value.p = v1 -> value.i + (double *)(v2 -> value.p);
								v1 -> ind_level = v2 -> ind_level;
								v1 -> type = DT_DOUBLE;
							}
							else{
								v1 -> value.d = v1 -> value.i + v2 -> value.d;
								v1 -> type = DT_DOUBLE;
							}
					}
					break;
				case DT_FLOAT:
					switch(v2 -> type){
						case DT_CHAR:
							if(v1 -> ind_level)
								v1 -> value.p = (float *)(v1 -> value.p) + v2 -> value.c;
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else
								v1 -> value.f = v1 -> value.f + v2 -> value.c;
							break;
						case DT_INT:
							if(v1 -> ind_level)
								v1 -> value.p = (float *)(v1 -> value.p) + v2 -> value.i;
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else
								v1 -> value.f = v1 -> value.f + v2 -> value.i;
							break;
						case DT_FLOAT:
							if(v1 -> ind_level || v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else
								v1 -> value.f = v1 -> value.f + v2 -> value.f;
							break;
						case DT_DOUBLE:
							if(v1 -> ind_level || v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else{
								v1 -> value.d = v1 -> value.f + v2 -> value.d;
								v1 -> type = DT_DOUBLE;
							}
					}
					break;
				case DT_DOUBLE:
					switch(v2 -> type){
						case DT_CHAR:
							if(v1 -> ind_level)
								v1 -> value.p = (double *)(v1 -> value.p) + v2 -> value.c;
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else
								v1 -> value.d = v1 -> value.d + v2 -> value.c;
							break;
						case DT_INT:
							if(v1 -> ind_level)
								v1 -> value.p = (double *)(v1 -> value.p) + v2 -> value.i;
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else
								v1 -> value.d = v1 -> value.d + v2 -> value.i;
							break;
						case DT_FLOAT:
							if(v1 -> ind_level || v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else
								v1 -> value.d = v1 -> value.d + v2 -> value.f;
							break;
						case DT_DOUBLE:
							if(v1 -> ind_level || v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else
								v1 -> value.d = v1 -> value.d + v2 -> value.d;
					}
					break;
			}
			break;
		case MINUS:
			if(v1 -> ind_level && v2 -> ind_level){
				v1 -> type = DT_INT;
				v1 -> ind_level = 0;
				v1 -> value.i = v1 -> value.p - v2 -> value.p;
				return;
			}
			switch(v1 -> type){
				case DT_CHAR:
					switch(v2 -> type){
						case DT_CHAR:
							if(v1 -> ind_level)
								v1 -> value.p = v1 -> value.p - v2 -> value.c;
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS); // not possible to subtract a pointer from a char
							else{
								v1 -> value.i = v1 -> value.c - v2 -> value.c;
								v1 -> type = DT_INT;
							}
							break;
						case DT_INT:
							if(v1 -> ind_level)
								v1 -> value.p = (char *)v1 -> value.p - v2 -> value.i;
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS); // not possible to subtract a pointer from a char
							else{
								v1 -> value.i = v1 -> value.c - v2 -> value.i;
								v1 -> type = DT_INT;
							}
							break;
						case DT_FLOAT:
							if(v1 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS); // not possible to subtract a pointer from a char
							else{
								v1 -> value.f = v1 -> value.c - v2 -> value.f;
								v1 -> type = DT_FLOAT;
							}
							break;
						case DT_DOUBLE:
							if(v1 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS); // not possible to subtract a pointer from a char
							else{
								v1 -> value.d = v1 -> value.c - v2 -> value.d;
								v1 -> type = DT_DOUBLE;
							}
					}
					break;
				case DT_INT:
					switch(v2 -> type){
						case DT_CHAR:
							if(v1 -> ind_level)
								v1 -> value.p = (int *)v1 -> value.p - v2 -> value.c;
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS); // not possible to subtract a pointer from an int
							else
								v1 -> value.i = v1 -> value.i - v2 -> value.c;
							break;
						case DT_INT:
							if(v1 -> ind_level)
								v1 -> value.p = (int *)v1 -> value.p - v2 -> value.i;
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS); // not possible to subtract a pointer from an int
							else
								v1 -> value.i = v1 -> value.i - v2 -> value.i;
							break;
						case DT_FLOAT:
							if(v1 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS); // not possible to subtract a pointer from an int
							else{
								v1 -> value.f = v1 -> value.i - v2 -> value.f;
								v1 -> type = DT_FLOAT;
							}
							break;
						case DT_DOUBLE:
							if(v1 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS); // not possible to subtract a pointer from an int
							else{
								v1 -> value.d = v1 -> value.i - v2 -> value.d;
								v1 -> type = DT_DOUBLE;
							}
					}
					break;
				case DT_FLOAT:
					switch(v2 -> type){
						case DT_CHAR:
							if(v1 -> ind_level)
								v1 -> value.p = (float *)v1 -> value.p - v2 -> value.c;
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else
								v1 -> value.f = v1 -> value.f - v2 -> value.c;
							break;
						case DT_INT:
							if(v1 -> ind_level)
								v1 -> value.p = (float *)v1 -> value.p - v2 -> value.i;
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else
								v1 -> value.f = v1 -> value.f - v2 -> value.i;
							break;
						case DT_FLOAT:
							if(v1 -> ind_level || v2 -> ind_level)
								show_error(POINTER_SYNTAX);
							else
								v1 -> value.f = v1 -> value.f - v2 -> value.f;
							break;
						case DT_DOUBLE:
							if(v1 -> ind_level || v2 -> ind_level)
								show_error(POINTER_SYNTAX);
							else
								v1 -> value.d = v1 -> value.f - v2 -> value.d;
					}
					break;
				case DT_DOUBLE:
					switch(v2 -> type){
						case DT_CHAR:
							if(v1 -> ind_level)
								v1 -> value.p = (double *)v1 -> value.p - v2 -> value.c;
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else
								v1 -> value.d = v1 -> value.d - v2 -> value.c;
							break;
						case DT_INT:
							if(v1 -> ind_level)
								v1 -> value.p = (float *)v1 -> value.p - v2 -> value.i;
							else if(v2 -> ind_level)
								show_error(INVALID_BINARY_OPERANDS);
							else
								v1 -> value.d = v1 -> value.d - v2 -> value.i;
							break;
						case DT_FLOAT:
							if(v1 -> ind_level || v2 -> ind_level)
								show_error(POINTER_SYNTAX);
							else
								v1 -> value.d = v1 -> value.d - v2 -> value.f;
							break;
						case DT_DOUBLE:
							if(v1 -> ind_level || v2 -> ind_level)
								show_error(POINTER_SYNTAX);
							else
								v1 -> value.d = v1 -> value.d - v2 -> value.d;
					}
					break;
			}
			break;
		case ASTERISK:
			// pointers are not allowed in multiplication
			if(v1 -> ind_level || v2 -> ind_level) show_error(INVALID_BINARY_OPERANDS);
			switch(v1 -> type){
				case DT_CHAR:
					switch(v2 -> type){
						case DT_CHAR:
							v1 -> value.i = v1 -> value.c * v2 -> value.c;
							v1 -> type = DT_INT;
							break;
						case DT_INT:
							v1 -> value.i = v1 -> value.c * v2 -> value.i;
							v1 -> type = DT_INT;
							break;
						case DT_FLOAT:
							v1 -> value.f = v1 -> value.c * v2 -> value.f;
							v1 -> type = DT_FLOAT;
							break;
						case DT_DOUBLE:
							v1 -> value.d = v1 -> value.c * v2 -> value.d;
							v1 -> type = DT_DOUBLE;
					}
					break;
				case DT_INT:
					switch(v2 -> type){
						case DT_CHAR:
							v1 -> value.i = v1 -> value.i * v2 -> value.c;
							v1 -> type = DT_INT;
							break;
						case DT_INT:
							v1 -> value.i = v1 -> value.i * v2 -> value.i;
							break;
						case DT_FLOAT:
							v1 -> value.f = v1 -> value.i * v2 -> value.f;
							v1 -> type = DT_FLOAT;
							break;
						case DT_DOUBLE:
							v1 -> value.d = v1 -> value.i * v2 -> value.d;
							v1 -> type = DT_DOUBLE;
					}
					break;
				case DT_FLOAT:
					switch(v2 -> type){
						case DT_CHAR:
							v1 -> value.f = v1 -> value.f * v2 -> value.c;
							break;
						case DT_INT:
							v1 -> value.f = v1 -> value.f * v2 -> value.i;
							break;
						case DT_FLOAT:
							v1 -> value.f = v1 -> value.f * v2 -> value.f;
							break;
						case DT_DOUBLE:
							v1 -> value.d = v1 -> value.f * v2 -> value.d;
							v1 -> type = DT_DOUBLE;
					}
					break;
				case DT_DOUBLE:
					switch(v2 -> type){
						case DT_CHAR:
							v1 -> value.d = v1 -> value.d * v2 -> value.c;
							break;
						case DT_INT:
							v1 -> value.d = v1 -> value.d * v2 -> value.i;
							break;
						case DT_FLOAT:
							v1 -> value.d = v1 -> value.d * v2 -> value.f;
							break;
						case DT_DOUBLE:
							v1 -> value.d = v1 -> value.d * v2 -> value.d;
					}
					break;
			}
			break;
		case DIVISION:
			// pointers are not allowed in div
			if(v1 -> ind_level || v2 -> ind_level) show_error(INVALID_BINARY_OPERANDS);
			switch(v1 -> type){
				case DT_CHAR:
					switch(v2 -> type){
						case DT_CHAR:
							v1 -> value.c = v1 -> value.c / v2 -> value.c;
							break;
						case DT_INT:
							v1 -> value.i = v1 -> value.c / v2 -> value.i;
							v1 -> type = DT_INT;
							break;
						case DT_FLOAT:
							v1 -> value.f = v1 -> value.c / v2 -> value.f;
							v1 -> type = DT_FLOAT;
							break;
						case DT_DOUBLE:
							v1 -> value.d = v1 -> value.c / v2 -> value.d;
							v1 -> type = DT_DOUBLE;
					}
					break;
				case DT_INT:
					switch(v2 -> type){
						case DT_CHAR:
							v1 -> value.i = v1 -> value.i / v2 -> value.c;
							break;
						case DT_INT:
							v1 -> value.i = v1 -> value.i / v2 -> value.i;
							break;
						case DT_FLOAT:
							v1 -> value.f = v1 -> value.i / v2 -> value.f;
							v1 -> type = DT_FLOAT;
							break;
						case DT_DOUBLE:
							v1 -> value.d = v1 -> value.i / v2 -> value.d;
							v1 -> type = DT_DOUBLE;
					}
					break;
				case DT_FLOAT:
					switch(v2 -> type){
						case DT_CHAR:
							v1 -> value.f = v1 -> value.f / v2 -> value.c;
							break;
						case DT_INT:
							v1 -> value.f = v1 -> value.f / v2 -> value.i;
							break;
						case DT_FLOAT:
							v1 -> value.f = v1 -> value.f / v2 -> value.f;
							break;
						case DT_DOUBLE:
							v1 -> value.d = v1 -> value.f / v2 -> value.d;
							v1 -> type = DT_DOUBLE;
					}
					break;
				case DT_DOUBLE:
					switch(v2 -> type){
						case DT_CHAR:
							v1 -> value.d = v1 -> value.d / v2 -> value.c;
							break;
						case DT_INT:
							v1 -> value.d = v1 -> value.d / v2 -> value.i;
							break;
						case DT_FLOAT:
							v1 -> value.d = v1 -> value.d / v2 -> value.f;
							break;
						case DT_DOUBLE:
							v1 -> value.d = v1 -> value.d / v2 -> value.d;
					}
			}
			break;
		case MOD:
			// pointers are not allowed in mod
			if(v1 -> ind_level || v2 -> ind_level) show_error(INVALID_BINARY_OPERANDS);
			switch(v1 -> type){
				case DT_CHAR:
					switch(v2 -> type){
						case DT_CHAR:
							v1 -> value.c = v1 -> value.c % v2 -> value.c;
							break;
						case DT_INT:
							v1 -> value.i = v1 -> value.c % v2 -> value.i;
							v1 -> type = DT_INT;
							break;
						case DT_FLOAT:
						case DT_DOUBLE:
							show_error(INVALID_BINARY_OPERANDS);
					}
					break;
				case DT_INT:
					switch(v2 -> type){
						case DT_CHAR:
							v1 -> value.i = v1 -> value.i % v2 -> value.c;
							break;
						case DT_INT:
							v1 -> value.i = v1 -> value.i % v2 -> value.i;
							break;
						case DT_FLOAT:
						case DT_DOUBLE:
							show_error(INVALID_BINARY_OPERANDS);
					}
					break;
				case DT_FLOAT:
				case DT_DOUBLE:
					show_error(INVALID_BINARY_OPERANDS);
			}
			break;
			
	}
}

char isdelim(char c){
	if(strchr("#+-*/%[](){};,<>=!&|~.", c)) return 1;
	else return 0;
}

char is_idchar(char c){
	return(isalpha(c) || c == '_');
}

void include_lib(char *lib_name){
	register int i, j;

	for(i = 0; *libs[i].lib_name; i++){
		if(!strcmp(libs[i].lib_name, lib_name)){
			// adds the functions to the active function table
			for(j = 0; *libs[i].libp[j].func_name; j++){
				strcpy(active_func_table[active_func_table_tos].func_name, libs[i].libp[j].func_name);
				active_func_table[active_func_table_tos].fp = libs[i].libp[j].fp;
				active_func_table_tos++;
			}
			// adds the constants to the active constant table
			for(j = 0; *libs[i].constp[j].str; j++){
				strcpy(active_const_table[active_const_table_tos].str, libs[i].constp[j].str);
				active_const_table[active_const_table_tos].data = libs[i].constp[j].data;
				active_const_table_tos++;
			}
			return;
		}
	}
	
	show_error(UNKOWN_LIBRARY);
}
