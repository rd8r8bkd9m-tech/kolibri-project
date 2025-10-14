#include <stdio.h>

void test_decimal(void);
void test_genome(void);
void test_formula(void);
void test_net(void);
void test_digits(void);
void test_script(void);
void test_script_crystal_cycle(void);
void test_script_load_file(void);
void test_knowledge_index(void);
void test_knowledge_queue(void);
void test_sim(void);
void test_public_api(void);

int main(void) {
  test_decimal();
  test_genome();
  test_formula();
  test_digits();
  test_net();
  test_script();
  test_script_crystal_cycle();
  test_script_load_file();
  test_knowledge_index();
  test_knowledge_queue();
  test_sim();
  test_public_api();
  printf("all tests passed\n");
  return 0;
}
