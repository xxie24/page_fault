#include <stdlib.h>
#include <stdbool.h>
#define list_size 10000

struct list;
struct list {
	struct list *prev;
	struct list *next;
};
struct list *list_head;
struct list *list_tail;
struct list *list_array[list_size];

static void *get_fragmented_memory_page()
{
	const unsigned int large_size = 4096UL;
	const unsigned int small_size = 37UL;
	void *s_mem;
	void *l_mem;

	s_mem = malloc(small_size);
	l_mem = malloc(large_size);
	if (l_mem == NULL || s_mem == NULL)
		exit(EXIT_FAILURE);
	free(s_mem);

	return l_mem;
}

static struct list *add_link_list(struct list *node)
{
	if (list_tail) {
		list_tail->next = node;
		node->prev = list_tail;
		list_tail = node;
	} else {
		list_head = node;
		list_tail = node;
	}
	return node;
}

void shuffle(int array[], size_t n)
{
	size_t i, j;
	int temp;

	if (n <= 1)
		return;
	for (i = 0; i < n - 1; i++) {
		j = rand() % n;
		temp = array[j];
		array[j] = array[i];
		array[i] = temp;
	}
}

void *tlb_thrashing_func(void *t)
{
	int i;
	struct list *node;
	int list_index[list_size];

	/* initialize list 1st */
	list_head = list_tail = NULL;
	for (i = 0; i < list_size; i++) {
		list_array[i] = get_fragmented_memory_page();
		list_index[i] = i;
	}
	/* shuffle the array */
	shuffle(list_index, list_size);
	/* create the randomize list */
	for (i = 0; i < list_size; i++)
		add_link_list(list_array[list_index[i]]);

	/* walk the list forever*/
	while (true)
		for (node = list_head; node != list_tail; node = node->next)
			continue;
	return NULL;
}

void *page_fault_func(void *t)
{
	int i;
	int *allocated[list_size];

	while (true) {
		for (i = 0; i < list_size; i++)
			allocated[i] = get_fragmented_memory_page();
		for (i = 0; i < list_size; i++)
			*allocated[i] = 0xFF;
		for (i = 0; i < list_size; i++)
			free(allocated[i]);
	}
	return NULL;
}

