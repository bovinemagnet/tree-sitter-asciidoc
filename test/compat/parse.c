/*
 * Parses each file named on the command line with the grammar selected by
 * -DLANGUAGE_FN and prints "<file>\t<s-expression>" per file. Built against
 * an older tree-sitter runtime by run.sh to check that the committed parser
 * loads and parses there. Prints "REJECTED" and fails when the runtime
 * refuses the parser, which is what an ABI it cannot load looks like.
 */
#include <stdio.h>
#include <stdlib.h>
#include <tree_sitter/api.h>

const TSLanguage *LANGUAGE_FN(void);

int main(int argc, char **argv) {
  TSParser *parser = ts_parser_new();
  if (!ts_parser_set_language(parser, LANGUAGE_FN())) {
    puts("REJECTED");
    return 1;
  }
  for (int i = 1; i < argc; i++) {
    FILE *file = fopen(argv[i], "rb");
    if (!file) {
      perror(argv[i]);
      return 1;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    char *source = malloc(length + 1);
    if (fread(source, 1, length, file) != (size_t)length) {
      perror(argv[i]);
      return 1;
    }
    fclose(file);
    TSTree *tree = ts_parser_parse_string(parser, NULL, source, length);
    char *sexp = ts_node_string(ts_tree_root_node(tree));
    printf("%s\t%s\n", argv[i], sexp);
    free(sexp);
    ts_tree_delete(tree);
    free(source);
  }
  ts_parser_delete(parser);
  return 0;
}
