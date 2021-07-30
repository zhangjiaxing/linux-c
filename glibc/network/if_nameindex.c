#include <stdio.h>
#include <net/if.h>
#include <unistd.h>
#include <stdlib.h>


int main(void){
    struct if_nameindex *i;
    struct if_nameindex *ptr = if_nameindex();

	if (ptr == NULL) {
    	perror("if_nameindex");
        exit(EXIT_FAILURE);
	}

    for (i = ptr; ! (i->if_index == 0 && i->if_name == NULL); i++){
		printf("%u: %s\n", i->if_index, i->if_name);
	}

    if_freenameindex(ptr);
    return 0;
}
