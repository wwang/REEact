/**********************************************************************************
 * This is a template class for static linked. Any array wants to used this template
 * has to have "prev, next" at the beginning of each list item. The head and tail
 * of the linked list should be stored separated. See example of task list in 
 * autotuner shared memory data structure.
 * A pointer with value -1 is similar to a NULL pointer in real linked list
 * Parameter "size" in each function is used to reference the real list item size
 *********************************************************************************/


#ifndef __STRATA_AUTOTUNER_STATIC_LINKED_LIST__
#define __STRATA_AUTOTUNER_STATIC_LINKED_LIST__

typedef struct _at_slist_item{
  int valid;
  int prev;
  int next;
}at_slist_item;

//insert item at "index" into static linked list slist
int at_slist_insert(int * head, int * tail, void * slist, int size, int index);
//remove item at "index" from static linked list slist
int at_slist_remove(int * head, int * tail, void * slist, int size, int index);
//get next item of item at "index", next item stored in return value
int at_slist_next(void * slist, int size, int index);


#endif
