#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <unistd.h>
#include <endian.h>
#include <dirent.h>
#include <byteswap.h>
#include <sys/mman.h>
#include <pthread.h>


struct compressed_bit{
    char* load;
    uint8_t reps;
    int size;
};

struct tree{
    struct tree* left;
    struct tree* right;
    struct compressed_bit* value;
};

struct decompressed{
    char* load;
    uint64_t size;
};

struct multiplex{
    uint32_t id;
    uint64_t start;
    uint64_t range;
};

void error(int client_sock){
    uint8_t ret[9];
    ret[0] = 0xf0;
    int i = 1;
    while(i<9){
        ret[i] = 0x00;
        i++;
    }
    send(client_sock, &ret, 9, 0);
}

void reset_char(char* reset, uint64_t size){
    int i =0;
    while(i<size){
        reset[i] = 0x00;
        i++;
    }
}

char* compress(char* payload, uint64_t pay_length, struct compressed_bit** dictionary, char* ret, uint64_t ret_length){
    reset_char(ret, ret_length);
    int offset = 0;
    int i = 0;
    while(i < pay_length){
        uint8_t index = payload[i];
        struct compressed_bit* byte = dictionary[index];
        int j = 0;
        while(j<byte->size){
            uint8_t temp;
            if(byte->load[j] == '1'){
                temp = 0x01;
                temp = temp << (7 - (offset%8));
                ret[offset/8] = ret[offset/8]|temp;
            }
            j++;
            offset++;
        }
        i++;
    }
    if((offset%8) == 0){
        ret[ret_length] = 0x00;
    }
    else{
        ret[ret_length] = 8-(offset%8);
    }
    return ret;
}

uint64_t compress_size(char* payload, uint64_t length, struct compressed_bit** dictionary){
    uint64_t size = 0;//size after compression
    int i = 0;
    while(i<length){
        uint8_t index = payload[i];
        size+=dictionary[index]->size;
        i++;
    }
    if((size%8) == 0){
        return size/8;
    }
    //fprintf(stderr, "THIS: %ld\n",size);
    return (size/8)+1;
}

struct tree* build_tree(struct compressed_bit** dictionary, struct tree* target){
    int i = 0;
    while(i < 256){
        struct compressed_bit* temp = dictionary[i];
        int j = 0;
        struct tree* current = target;
        while(j<temp->size){
            if(temp->load[j] == '0'){
                if(current->left == NULL){
                    current->left = malloc(sizeof(struct tree));
                    current->left->value = NULL;
                    current->left->left = NULL;
                    current->left->right = NULL;
                }
                current = current->left;
            }

            else if(temp->load[j] == '1'){
                if(current->right == NULL){
                    current->right = malloc(sizeof(struct tree));
                    current->right->value = NULL;
                    current->right->left = NULL;
                    current->right->right = NULL;
                }
                current = current->right;
            }
            j++;
        }
        current->value = temp;
        i++;
    }

    return target;
}

struct decompressed* decompress(struct tree* dict, char* payload, uint64_t size){
    uint8_t pad = payload[size-1];
    uint64_t decomp_size = 0;
    char* decomp = NULL;
    size = size-1;
    uint64_t to_decompress = size * 8;
    to_decompress-=pad;
    int offset =0;
    struct tree* current = dict;
    while(offset<to_decompress){

        uint8_t diff = offset%8;
        uint8_t temp = payload[offset/8];
        temp = temp << diff;
        temp = temp >> 7;

        if(temp == 0x01){
            current = current->right;
        }
        else if(temp == 0x00){
            current = current->left;
        }

        if(current->value != NULL){
            decomp_size++;
            decomp = realloc(decomp, decomp_size);
            decomp[decomp_size-1] = current->value->reps;
            current = dict;
        }
        offset++;
    }
    struct decompressed* ret = malloc(sizeof(struct decompressed));
    ret->load = decomp;
    ret->size = decomp_size;

    return ret;
}

struct compressed_bit** build_dict(char* dict){
      int offset = 0;
      int filled = 0;
      struct compressed_bit** ret = malloc(sizeof(struct compressed_bit*) * 256);
      while(filled<256){
        uint8_t size = 0x00;
        int count = 0;
        while(count<8){
          uint8_t temp = dict[offset/8];
          temp = temp>>(7-(offset%8));
          temp = temp<<7;
          temp = temp>>count;
          size = size|temp;
          offset++;
          count++;
        }

        count = 0;
        ret[filled] = malloc(sizeof(struct compressed_bit));
        ret[filled]->size = size;
        ret[filled]->reps = filled;
        ret[filled]->load = malloc(size);
        while(count<size){
          uint8_t temp = dict[offset/8];
          temp = temp>>(7-(offset%8));
          temp = temp <<7;
          temp = temp >>7;
          if(temp == 0){
            ret[filled]->load[count]= '0';

          }
          else if(temp == 1){
            ret[filled]->load[count] = '1';

          }
          offset++;
          count++;
        }
        filled++;
      }
      return ret;
}

    /*
     * USYD CODE CITATION ACKNOWLEDGEMENT
     * Code for socket setup based on Seminar 11 demonstration
     */

int main(int argc, char** argv){
      int option = 1;
      //read info from file
      FILE* f = fopen(argv[1], "rb");
      in_addr_t ip;
      uint16_t port;
      fread(&ip, 4, 1, f);
      fread(&port, 2, 1, f);

      //TARGET DIRECTORY (COMMENTED OUT BECAUSE "UNUSED VALUE")
      char target[50];
      int i = 0;
      while(i<50){
        char temp= fgetc(f);
        if(temp== EOF){
          target[i] = '\0';
          break;
        }
        target[i]=temp;
        i++;
      }
      //TARGET DIRECTORY

      fclose(f);

      //compression dictionary
      FILE* dict = fopen("compression.dict", "rb");
      char c;
      char* temp_dict = NULL;
      int dict_size = 0;
      while((c = fgetc(dict)) != EOF){
        dict_size++;
        temp_dict = realloc(temp_dict, dict_size);
        temp_dict[dict_size-1] = c;
      }
      fclose(dict);

      struct compressed_bit** dictionary= build_dict(temp_dict);

      free(temp_dict);

      struct tree* decomp_tree = malloc(sizeof(struct tree));
      decomp_tree->value = NULL;
      decomp_tree->left = NULL;
      decomp_tree->right = NULL;

      decomp_tree = build_tree(dictionary, decomp_tree);


      /*
      * USYD CITATION ACKNOWLEDGEMENT
      * mmap taken from:
      * https://www.youtube.com/watch?v=rPV6b8BUwxM
      */

      struct multiplex* sessions = mmap(NULL, sizeof(struct multiplex) * 1024, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
      int* sessions_size = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
      *sessions_size = 0;
      pthread_mutex_t* sessions_lock = mmap(NULL, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
      pthread_mutex_init(sessions_lock, NULL);

      //end of copied code


      int sock = socket(AF_INET, SOCK_STREAM,0);

      if(sock<0){
        exit(1);
      }

      struct sockaddr_in address;
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = ip;
      address.sin_port = port;

      setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option, sizeof(int));

      bind(sock, (struct sockaddr*)&address, sizeof(struct sockaddr_in));

      listen(sock, 10);

      while(1){
        uint32_t addlen = sizeof(struct sockaddr_in);
        int client_sock = accept(sock, (struct sockaddr*) &address, &addlen);

        int pid = fork();

        if(pid == 0){
          while(1){
            uint8_t header;//Message
            read(client_sock, &header, 1);
            uint8_t type = header >> 4; //type of instruction
            uint8_t pressed = header<<4; //Compression bit
            pressed = pressed>>7;
            uint8_t req_press = header << 5;
            req_press = req_press>>7;

            uint64_t length; //Length of payload
            read(client_sock, &length, 8);
            uint64_t size = be64toh(length);
            char* payload = NULL; //payload
            if(size > 0 && type!=6){
              payload = malloc(sizeof(char) * size);
              read(client_sock, payload, size);
            }
            if(type == 0){
              char* compressed = NULL;
              uint64_t temp_size = 0;
              if(req_press == 1 && pressed == 0){

                temp_size = compress_size(payload, size, dictionary);
                compressed = malloc(temp_size+1);
                compressed = compress(payload, size, dictionary, compressed, temp_size);
                free(payload);
                payload = NULL;
                payload = compressed;
                size = temp_size+1;
                length = bswap_64(size);
              }
              char* ret = malloc(sizeof(char) * (9+size));
              ret[0] = header | 0x10;
              ret[0] = ret[0] & 0xf8;
              if(req_press == 1 || pressed == 1){
                ret[0] = ret[0] |0x08;
              }
              memcpy(&ret[1], &length, 8);
              memcpy(&ret[9], payload, size);
              send(client_sock, ret, 9+size, 0);
              free(ret);
            }
            else if(type == 2){
              /*
               * USYD CODE CITATION ACKNOWLEDGEMENT
               * Learned about DIR and adapted code from Geeks for Geeks
               * https://www.geeksforgeeks.org/c-program-list-files-sub-directories-directory/
               */
              DIR* d = opendir(target);
              struct dirent* f = NULL;
              char* ret = NULL;//return pay
              uint64_t start = 0;
              while((f = readdir(d)) != NULL){
                if(f->d_type == DT_REG){
                  ret = realloc(ret, start + strlen(f->d_name)+1);
                  memcpy(&ret[start], f->d_name, strlen(f->d_name));
                  ret[start+strlen(f->d_name)] = 0x00;
                  start += strlen(f->d_name) +1;
                }
              }
              closedir(d);
              /*
               * End of copied code
               */

               char* compressed = NULL;
               uint64_t temp_size = 0;
               if(req_press == 1 && pressed == 0){

                 temp_size = compress_size(ret, start, dictionary);
                 compressed = malloc(temp_size+1);
                 compressed = compress(ret, start, dictionary, compressed, temp_size);
                 free(ret);
                 ret = NULL;
                 ret = compressed;
                 start = temp_size+1;
                 length = bswap_64(start);
               }

              char* final = malloc(start+9);
              final[0] = header | 0x30;
              final[0] = final[0] &0xf8;

              if(req_press == 1){
                final[0] = final[0] | 0x08;
              }

              uint64_t length = bswap_64(start);
              memcpy(&final[1], &length, 8);
              memcpy(&final[9], ret, start);
              send(client_sock, final, 9+start, 0);
              free(ret);
              free(final);
            }
            else if(type == 4){

              if(pressed == 1){
                struct decompressed* temp = decompress(decomp_tree, payload, size);
                free(payload);
                payload = temp->load;
                size = temp->size;
                length = bswap_64(size);
                free(temp);
                pressed = 0;
              }



              if(payload == NULL){
                error(client_sock);
                continue;
              }
              char filename[100];

              memcpy(filename, target, strlen(target));
              filename[strlen(target)] = '/';
              memcpy(&filename[strlen(target)+1], payload, size);
              FILE* fs = fopen(filename, "r");
              if(fs == NULL){
                error(client_sock);
                free(payload);
                continue;
              }
              else{
                uint64_t fsize=0;
                //Add 1 for each character (1 byte) read
                while(fgetc(fs) != EOF){
                  fsize++;
                }
                fclose(fs);
                fsize = bswap_64(fsize);
                free(payload);
                payload = malloc(8);
                memcpy(payload, &fsize, 8);

                char* compressed = NULL;
                uint64_t temp_size = 0;
                size = 8;
                if(req_press == 1 && pressed == 0){

                  temp_size = compress_size(payload, 8, dictionary);
                  compressed = malloc(temp_size+1);
                  compressed = compress(payload, 8, dictionary, compressed, temp_size);
                  free(payload);
                  payload = NULL;
                  payload = compressed;
                  size = temp_size+1;
                }
                length = bswap_64(size);
                char* ret = malloc(9+size); //1 for header, 8 for size, 8 for payload
                ret[0] = 0x50;
                if(req_press == 1){
                  ret[0] = ret[0] | 0x08;
                }

                memcpy(&ret[1],&length, 8);
                memcpy(&ret[9], payload, size);
                send(client_sock, ret, 9+size, 0);
                free(ret);
              }
            }
            else if(type == 6){ //RETRIEVE FILE
              uint32_t id;
              uint64_t start;
              uint64_t range;
              uint64_t fixed_start;
              uint64_t fixed_range;
              char fpath[100];

              if(pressed == 0){
                payload = malloc(size - 20);
                read(client_sock, &id, 4);
                read(client_sock, &start, 8);
                fixed_start = be64toh(start);
                read(client_sock, &range, 8);
                fixed_range = be64toh(range);
                read(client_sock, payload, size-20);

              }

              if(pressed == 1){
                payload = malloc(size);
                read(client_sock, payload, size);
                struct decompressed* temp = decompress(decomp_tree, payload, size);
                free(payload);
                payload = temp->load;
                size = temp->size;
                free(temp);
                memcpy(&id, payload, 4);
                memcpy(&start, &payload[4], 8);
                memcpy(&range, &payload[12], 8);
                char* temp_pay = malloc(size-20);
                memcpy(temp_pay, &payload[20], size-20);
                fixed_range = be64toh(range);
                fixed_start = be64toh(start);

                free(payload);
                payload = temp_pay;
                pressed = 0;
              }

              //CHECK IDS

              pthread_mutex_lock(sessions_lock);

              int i = 0;
              int match = 0;

              while(i<*sessions_size){
                  if(id == sessions[i].id && sessions[i].start == fixed_start && sessions[i].range == fixed_range){
                      match = 1;
                      break;
                  }
                  i++;
              }


              if(match == 1){
                  pthread_mutex_unlock(sessions_lock);
                  error(client_sock);
                  free(payload);
                  exit(client_sock);
                  //break;
              }


              sessions[*sessions_size].id = id;
              sessions[*sessions_size].start = fixed_start;
              sessions[*sessions_size].range = fixed_range;
              *sessions_size+=1;

              pthread_mutex_unlock(sessions_lock);


              sprintf(fpath, "%s/%s", target, payload);
              FILE* f = fopen(fpath, "r");

              //Non-existent file
              if(f == NULL){
                error(client_sock);
                free(payload);
                continue;
              }
              fseek(f, 0, SEEK_END);
              uint64_t counter = ftell(f);
              fseek(f, 0, SEEK_SET);

              char* content = malloc(counter+1);
              fread(content, 1, counter, f);
              fclose(f);


              //BAD  RANGE
              if((counter < fixed_start + fixed_range) || fixed_start < 0 || fixed_range <0){
                error(client_sock);
                free(content);
                free(payload);
                continue;
              }


              free(payload);
              uint64_t ret_size = 20+fixed_range;

              payload = malloc(ret_size);
              memcpy(payload, &id, 4);
              memcpy(&payload[4], &start, 8);
              memcpy(&payload[12], &range, 8);
              memcpy(&payload[20], &content[fixed_start], fixed_range);

              char* compressed = NULL;
              uint64_t temp_size = 0;
              if(req_press == 1 && pressed == 0){

                temp_size = compress_size(payload, ret_size, dictionary);
                compressed = malloc(temp_size+1);
                compressed = compress(payload, ret_size, dictionary, compressed, temp_size);
                free(payload);
                payload = NULL;
                payload = compressed;
                ret_size = temp_size+1;

              }
              char* ret = malloc(9+ret_size); //predetermined header bits + content to copy
              ret[0] = 0x70;
              if(req_press == 1){
                ret[0] = ret[0] | 0x08;
              }
              length = bswap_64(ret_size);
              memcpy(&ret[1], &length, 8);
              memcpy(&ret[9], payload, ret_size);
              send(client_sock, ret, 9+ret_size, 0);
              free(ret);
              free(content);
            }
            else if(type == 8){
              close(client_sock);
              shutdown(client_sock, SHUT_RDWR);
              exit(0);
            }
            else{
              error(client_sock);
            }
            if(payload!=NULL){
              free(payload);
            }
          }
        }

        close(client_sock);
      }

      close(sock);


      return 0;
}
