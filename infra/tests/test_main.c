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

/* WAL тесты */
void test_wal_enable_disable(void);
void test_stream_append(void);
void test_genome_stats(void);
void test_read_block(void);
void test_iterate_blocks(void);
void test_wal_checkpoint(void);

int main(void) {
  test_decimal();
  test_genome();
  test_wal_enable_disable();
  test_stream_append();
  test_genome_stats();
  test_read_block();
  test_iterate_blocks();
  test_wal_checkpoint();
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
