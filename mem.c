/* On inclut l'interface publique */
#include "mem.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

/* Définition de l'alignement recherché
 * Avec gcc, on peut utiliser __BIGGEST_ALIGNMENT__
 * sinon, on utilise 16 qui conviendra aux plateformes qu'on cible
 */
#ifdef __BIGGEST_ALIGNMENT__
#define ALIGNMENT __BIGGEST_ALIGNMENT__
#else
#define ALIGNMENT 16
#endif

/* structure placée au début de la zone de l'allocateur

   Elle contient toutes les variables globales nécessaires au
   fonctionnement de l'allocateur

   Elle peut bien évidemment être complétée
*/
struct allocator_header {
        size_t memory_size;
	mem_fit_function_t *fit;
	struct fb *libre; 	//pointeur vers le prochain FB
};

/* La seule variable globale autorisée
 * On trouve à cette adresse le début de la zone à gérer
 * (et une structure 'struct allocator_header)
 */
static void* memory_addr;

static inline void *get_system_memory_addr() {
	return memory_addr;
}

static inline struct allocator_header *get_header() {
	struct allocator_header *h;
	h = get_system_memory_addr();
	return h;
}

static inline size_t get_system_memory_size() {
	return get_header()->memory_size;
}


struct fb {	//freeblock : 2 infos : taille + pointeur prochain fb
	size_t size;
	struct fb* next;
	/* ... */
};

struct db {	//datablock : 1 info : taille du block occupé
	size_t size;
};

void mem_init(void* mem, size_t taille)
{
    memory_addr = mem;
    *(size_t*)memory_addr = taille;
	assert(mem == get_system_memory_addr());
	assert(taille == get_system_memory_size());

	struct allocator_header* head_first = get_header(); //création du header principal
	head_first->memory_size = taille; // on définit la taille comme étant égale à celle passée en argument
	head_first->libre = mem + sizeof(struct allocator_header);	//le fb commence juste après le header
	head_first->libre->size = taille - sizeof(struct allocator_header);	//il possède une taille égale à la taile globale - celle du header
	head_first->libre->next = NULL; //le prochain fb n'existe pas 
	
	
	mem_fit(&mem_fit_first);
}

void mem_show(void (*print)(void *, size_t, int)) {
   //PARTIE ZONES LIBRES CONTIGUES//
	struct fb* current_fb = get_header()->libre;	//on récupère le premier fb 
	while(current_fb != NULL){
		size_t tmp = current_fb->size;// + sizeof(struct fb);
		if((void*)(current_fb) + tmp == (void*)current_fb->next){ //si l'adresse du fb pointe au meme endroit que le bloc suivant le fb étudié alors on fusionne
			current_fb->size += current_fb->next->size; //on additionne les tailles des deux fb qui se suivent
			current_fb->next->size = 0;	//on réduit la taille du deuxième à 0
			current_fb->next = current_fb->next->next; //on ne prend plus en compte le 2eme fb, qui est maintenant ignoré
		}
	current_fb = current_fb->next;	//on continue jusquà la fin de la taille memoire
	}

	//PARTIE MEM_SHOW//
	size_t taille = get_system_memory_size() - sizeof(struct allocator_header); // defini la taille totale de notre zone de travail
	struct fb* fz = get_header()->libre; //recupère l'adresse de la première zone libre
	size_t compt = 0; //pour compter notre avancement dans la mémoire

	/*variable de stockage*/
	void* current_zone = memory_addr + sizeof(struct allocator_header); // endroit où l'on stock la zone courante
	size_t tmpTaille =0; //endorit où l'on stock la taille de la zone courante
	int bin_libre; // valeur permettant de definir si la zone est libre ou non

	while (compt < taille ) {
	if(current_zone == fz){ //si l'adresse de la première zone est egale à l'adresse de la premier fb => c'est une zone libre sinon non
		//tmpTaille = sizeof(struct fb);
		bin_libre = 1;
		struct fb* z = (struct fb*) current_zone;
		tmpTaille = z->size;
		print(z,tmpTaille,bin_libre);
	}
	else{
		bin_libre = 0;
		tmpTaille = sizeof(struct db);
		struct db* z = (struct db*) current_zone;
		tmpTaille += z->size; //n'arrive pas à récupérer la valeur du blocs
		print(z,tmpTaille,bin_libre);
	//  printf("%ld\t%ld\n",(char*) z - (char*) get_system_memory_addr(), tmpTaille );
	}

	if((void*) current_zone +tmpTaille >= (void*) fz +fz->size && fz->next != NULL) //pour passer à la zone libre suivante au bon moment
		fz = fz -> next;

	current_zone += tmpTaille;
	compt += tmpTaille;

		/* ... */
	}
}

void *mem_alloc(size_t taille) {
	__attribute__((unused)) // juste pour que gcc compile ce squelette avec -Werror 
	size_t true_size = taille + sizeof(struct db);
    struct fb *fb =get_header()->fit(get_header()->libre, true_size); //récupère la zone libre dispo

	if (fb == NULL)	//si il n'y a pas de fb de taille suffisante, on n'alloue pas
		return NULL;
	
	fb->size -= true_size;	//on réduit la taille de notre freeblock
	struct db* new_db = (struct db*)((char*)fb + fb->size);	//on crée un nouveau datablock de taille suffisante
	new_db->size = taille;	//on lui donne la taille entrée en argument
	return new_db;
}

void mem_free(void* mem) {
    struct fb* last_fz = get_header()->libre; 	//accès à la première zone libre
	if(last_fz->next != NULL){
    while(mem > (void*) last_fz->next){	//on recherche la denière zone libre placée avant l'adresse memoire passée en argument
      if(last_fz->next != NULL){
        last_fz = last_fz->next;
      } else{
        break;
      }
    }
  }
	struct db* oz = (struct db*) mem;
	size_t size_oz = oz->size;	//on récupère la taille de la zone occupée

	struct fb* new_fb = (struct fb*)mem;
	/*
	on définit la taille du nouveau bloc comme étant égale à l'ancienne,
	moins la taille du header des db, plus la taille du header d'un fb.
	*/
	new_fb->size = size_oz - sizeof(struct db) + sizeof(struct fb);


	new_fb->next = last_fz->next; //on insère le nouveau fb en respectant les pointeurs vers les nouveaux fb
	last_fz->next = new_fb;

	//PARTIE ZONES LIBRES CONTIGUES//
	struct fb* current_fb = get_header()->libre; //on récupère le premier fb 
	while(current_fb != NULL){
		size_t tmp = current_fb->size;// + sizeof(struct fb);
		if((void*)(current_fb) + tmp == (void*)current_fb->next){ //si l'adresse du fb pointe au meme endroit que le bloc suivant le fb étudié alors on fusionne
			current_fb->size += current_fb->next->size; //on additionne les tailles des deux fb qui se suivent
			current_fb->next->size = 0;	//on réduit la taille du deuxième à 0
			current_fb->next = current_fb->next->next; //on ne prend plus en compte le 2eme fb, qui est maintenant ignoré
		}
	current_fb = current_fb->next;	//on continue jusquà la fin de la taille memoire
	}
}

void mem_fit(mem_fit_function_t *f) {
	get_header()->fit = f;
}

struct fb* mem_fit_first(struct fb *list, size_t size) {
	while(list != NULL){
		if(list->size >= size)
			return list;
		list = list->next;
	}
  return NULL;
}

/* Fonction à faire dans un second temps
 * - utilisée par realloc() dans malloc_stub.c
 * - nécessaire pour remplacer l'allocateur de la libc
 * - donc nécessaire pour 'make test_ls'
 * Lire malloc_stub.c pour comprendre son utilisation
 * (ou en discuter avec l'enseignant)
 */
size_t mem_get_size(void *zone) {
	/* zone est une adresse qui a été retournée par mem_alloc() */

	/* la valeur retournée doit être la taille maximale que
	 * l'utilisateur peut utiliser dans cette zone */

	struct db* oz = (struct db*) zone;
	size_t taille_dispo_oz = zone->size;
	taille_dispo_oz -= sizeof(struct db);	//on enlève la taille occupée par le header de la zone occupée
	return taille_dispo_oz;
}

/* Fonctions facultatives
 * autres stratégies d'allocation
 */
struct fb* mem_fit_best(struct fb *list, size_t size) {
	struct fb* res = list;
	while (list != NULL){
		if(list->size >= size && res->size > list->size){
			res = list
		}
		list = list->next;
	}
	return res;
}

struct fb* mem_fit_worst(struct fb *list, size_t size) {
	struct fb* res = list;
	while (list != NULL){
		if(list->size >= size && res->size < list->size){
			res = list
		}
		list = list->next;
	}
	return res;
}
