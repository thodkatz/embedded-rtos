#include<stdio.h>
#include<pthread.h>
#include<stdlib.h>
 
#define Buffer_Limit 10
 
int Buffer_Index_Value = 0;
char *Buffer_Queue;
 
pthread_mutex_t mutex_variable = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t Buffer_Queue_Not_Full = PTHREAD_COND_INITIALIZER;
pthread_cond_t Buffer_Queue_Not_Empty = PTHREAD_COND_INITIALIZER;
 
void *Consumer()
{
      while(1)
      {
            pthread_mutex_lock(&mutex_variable);
            if(Buffer_Index_Value == -1)
            {            
                  pthread_cond_wait(&Buffer_Queue_Not_Empty, &mutex_variable);
            }                
            printf("Consumer:%d\t", Buffer_Index_Value--);        
            pthread_mutex_unlock(&mutex_variable);        
            pthread_cond_signal(&Buffer_Queue_Not_Full);                
      }    
}
 
void *Producer()
{    
      while(1)
      { 
            pthread_mutex_lock(&mutex_variable);
            if(Buffer_Index_Value == Buffer_Limit)
            {                        
                  pthread_cond_wait(&Buffer_Queue_Not_Full, &mutex_variable);
            }                        
            Buffer_Queue[Buffer_Index_Value++] = '@';
            printf("Producer:%d\t", Buffer_Index_Value);
            pthread_mutex_unlock(&mutex_variable);
            pthread_cond_signal(&Buffer_Queue_Not_Empty);        
      }    
}
 
int main()
{    
      pthread_t producer_thread_id, consumer_thread_id;
      Buffer_Queue = (char *) malloc(sizeof(char) * Buffer_Limit);            
      pthread_create(&producer_thread_id, NULL, Producer, NULL);
      pthread_create(&consumer_thread_id, NULL, Consumer, NULL);
      pthread_join(producer_thread_id, NULL);
      pthread_join(consumer_thread_id, NULL);
      return 0;
}