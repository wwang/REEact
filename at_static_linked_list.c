#include "at_static_linked_list.h"

//insert item at "index" into static linked list slist
int at_slist_insert(int * head, int * tail, void * slist, int size, int index)
{
  if( *head == -1 && *tail == -1 )
  {
      //this is the first item in the lisk
      *head = index;
      *tail = index;
      ((at_slist_item*)(slist + index * size))->prev = -1;
      ((at_slist_item*)(slist + index * size))->next = -1;
  }
  else
  {
    //there are some items already
    ((at_slist_item*)(slist + *tail * size))->next = index;
    ((at_slist_item*)(slist + index * size))->prev = *tail;
    ((at_slist_item*)(slist + index * size))->next = -1;
    *tail = index;
   }
  
  return 0;
}

//remove item at "index" from static linked list slist
int at_slist_remove(int * head, int * tail, void * slist, int size, int index)
{
  ((at_slist_item*)(slist + index * size))->valid = 0;

  if( index == *head && index == *tail)
    {
      //removing the last itme
      *head = *tail = -1;
    }
  else if( index == *head )
    {
      //removing the head
      int next = ((at_slist_item*)(slist + index * size))->next;
      *head = next;
      ((at_slist_item*)(slist + next * size))->prev = -1;
    }
  else if( index == *tail )
    {
      //removing the tail
      int prev = ((at_slist_item*)(slist + index * size))->prev;
      *tail = prev;
      ((at_slist_item*)(slist + prev * size))->next = -1;
    }
  else 
    {
      //removing a cell in the list
      int prev = ((at_slist_item*)(slist + index * size))->prev;
      int next = ((at_slist_item*)(slist + index * size))->next;
      ((at_slist_item*)(slist + prev * size))->next = next;
      ((at_slist_item*)(slist + next * size))->prev = prev;
    }
 
  return 0;
}
//get next item of item at "index", next item stored in return value
int at_slist_next(void * slist, int size, int index)
{ 
  return ((at_slist_item*)(slist + index * size))->next;
}
