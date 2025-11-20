/* ReinosoA-clienteFTP.c - main, sendCmd, pasivo, y comandos extendidos */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h> // Para wait()
#include <netinet/in.h>
#include <arpa/inet.h>

extern int  errno;

/* Prototipos de las funciones de ayuda */
int errexit(const char *format, ...);
int connectTCP(const char *host, const char *service);
int passiveTCP(const char *service, int qlen); /* Para sockets pasivos (escucha) */

#define  LINELEN    256
#define  MAX_FILES  32

/* Prototipos de funciones internas */
void sendCmd(int s, char *cmd, char *res);
int pasivo(int s);
/* CORRECCIÓN 1: Actualizar el prototipo para aceptar 'int s' */
void get_local_ip_for_port(int s, char *ip_str);
int create_listen_socket();
int get_socket_port(int s);
void handle_mget(int s, char *linea);
void handle_mput(int s, char *linea);
void ayuda();


/* Envia cmds FTP al servidor, recibe respuestas y las despliega */
void sendCmd(int s, char *cmd, char *res) {
  int n;
  char buffer_cmd[LINELEN + 2];

  /* Copia el comando al buffer y asegura que no haya desbordamiento */
  strncpy(buffer_cmd, cmd, LINELEN);
  buffer_cmd[LINELEN] = '\0'; // Asegura terminación nula

  n = strlen(buffer_cmd);
  buffer_cmd[n] = '\r';		/* formatear cmd FTP: \r\n al final */
  buffer_cmd[n+1] = '\n';
  buffer_cmd[n+2] = '\0';

  if (write(s, buffer_cmd, n + 2) < 0) { /* envia cmd por canal de control */
      errexit("Error al escribir en el socket de control: %s\n", strerror(errno));
  }
  
  n = read(s, res, LINELEN);	/* lee respuesta del svr */
  if (n < 0) {
      errexit("Error al leer del socket de control: %s\n", strerror(errno));
  }
  res[n] = '\0';		/* despliega respuesta */
  printf("%s", res); // La respuesta del servidor ya debe incluir \n
}

/* envia cmd PASV; recibe IP,pto del SVR; se conecta al SVR y retorna sock conectado */
int pasivo (int s){
  int sdata;			/* socket para conexion de datos */
  int nport;			/* puerto (en numeros) en SVR */
  char cmd[LINELEN], res[LINELEN], *p;  /* comando y respuesta FTP */
  char host[64], port[8];	/* host y port del SVR (como strings) */
  int h1,h2,h3,h4,p1,p2;	/* octetos de IP y puerto del SVR */

  sprintf (cmd, "PASV");
  sendCmd(s, cmd, res);

  /* Analiza la respuesta (h1,h2,h3,h4,p1,p2) */
  p = strrchr(res, '(');
  if (!p) {
      printf("Error: Respuesta PASV inesperada.\n");
      return -1;
  }
  sscanf(p+1, "%d,%d,%d,%d,%d,%d", &h1,&h2,&h3,&h4,&p1,&p2);
  snprintf(host, 64, "%d.%d.%d.%d", h1,h2,h3,h4);
  nport = p1*256 + p2;
  snprintf(port, 8, "%d", nport);

  /* Se conecta al puerto de datos del servidor */
  sdata = connectTCP(host, port);
  if (sdata < 0) {
      errexit("Error al conectar socket de datos (PASV): %s\n", strerror(errno));
  }
  return sdata;
}

/* CORRECCIÓN 2: Actualizar la definición de la función para aceptar 'int s' */
/* Obtiene la IP local y la formatea para el comando PORT */
void get_local_ip_for_port(int s, char *ip_str) {
    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    
    /* Preguntamos al sistema: "¿Quién soy yo en esta conexión?" */
    /* Ahora 's' ya está declarado como argumento de la función */
    if (getsockname(s, (struct sockaddr *)&local_addr, &addr_len) < 0) {
        errexit("Error en getsockname: %s\n", strerror(errno));
    }

    char *ip = inet_ntoa(local_addr.sin_addr);
    strncpy(ip_str, ip, 64);
    /* Reemplaza '.' por ',' para el formato de PORT */
    char *p;
    for(p = ip_str; *p; ++p) {
        if(*p == '.') {
            *p = ',';
        }
    }
}

/* * Crea un socket de escucha en un puerto efímero.
 * Esta versión no usa passiveTCP("0") para evitar el error "can't get service entry".
 * En su lugar, hace el bind() manualmente.
 */
int create_listen_socket() {
    int s_listen;
    struct sockaddr_in sin;
    int qlen = 5; // Longitud de la cola de escucha

    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen < 0) {
        errexit("Error en socket() para listen: %s\n", strerror(errno));
    }

    /* Prepara la estructura de dirección */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY; // Escuchar en todas las IPs locales
    sin.sin_port = 0; // Pide al SO un puerto efímero

    /* Asigna el puerto */
    if (bind(s_listen, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        /* Si el bind falla, cierra el socket antes de salir */
        close(s_listen);
        errexit("Error en bind() para puerto efímero: %s\n", strerror(errno));
    }

    /* Pone el socket en modo escucha */
    if (listen(s_listen, qlen) < 0) {
        close(s_listen);
        errexit("Error en listen(): %s\n", strerror(errno));
    }
    
    return s_listen;
}


/* Obtiene el número de puerto de un socket de escucha */
int get_socket_port(int s) {
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(s, (struct sockaddr *)&sin, &len) < 0) {
        errexit("Error en getsockname: %s\n", strerror(errno));
    }
    return ntohs(sin.sin_port); /* Retorna el puerto en orden de host */
}

/* Maneja el comando mget (transferencias concurrentes) */
void handle_mget(int s, char *linea) {
    char *files[MAX_FILES];
    int num_files = 0;
    char *token;
    char cmd[LINELEN], res[LINELEN], data[LINELEN];
    char local_ip_str[64];
    pid_t pids[MAX_FILES];
    int listen_socks[MAX_FILES];
    int i, n, status;

    /* Parsear los nombres de archivo */
    token = strtok(linea, " "); // El primer token es "mget"
    while ((token = strtok(NULL, " ")) != NULL && num_files < MAX_FILES) {
        files[num_files] = token;
        pids[num_files] = -1; // Inicializar pids
        listen_socks[num_files] = -1; // Inicializar sockets
        num_files++;
    }

    if (num_files == 0) {
        printf("Uso: mget <archivo1> <archivo2> ...\n");
        return;
    }

    /* Obtener IP local para PORT */
    get_local_ip_for_port(s, local_ip_str);

    /* * BUCLE 1: Iniciar transferencias y leer respuestas de control.
     * Las transferencias de DATOS (hijos) ocurrirán en paralelo.
     * Las operaciones de CONTROL (padre) son secuenciales.
     */
    for (i = 0; i < num_files; i++) {
        /* Crear socket de escucha local */
        listen_socks[i] = create_listen_socket();
        int port = get_socket_port(listen_socks[i]);

        /* Enviar comando PORT */
        sprintf(cmd, "PORT %s,%d,%d", local_ip_str, port / 256, port % 256);
        sendCmd(s, cmd, res);
        if (strncmp(res, "200", 3) != 0) {
            printf("Error: Comando PORT falló para %s. Saltando...\n", files[i]);
            close(listen_socks[i]);
            continue;
        }

        /* Enviar comando RETR */
        sprintf(cmd, "RETR %s", files[i]);
        sendCmd(s, cmd, res);
        if (strncmp(res, "150", 3) != 0) {
            printf("Error: Comando RETR falló para %s. Saltando...\n", files[i]);
            close(listen_socks[i]);
            /* NOTA: El servidor puede enviar una respuesta de error (ej. 550) 
             * aquí, así que no necesitamos leer un 226.
             */
            if (strncmp(res, "550", 3) == 0) {
                // Es un error "File not found", no vendrá un 226.
            } else {
                // Leer cualquier otra respuesta para limpiar el buffer
                n = read(s, res, LINELEN);
                if (n > 0) res[n] = '\0';
                printf("%s", res);
            }
            continue;
        }

        /* fork() para manejar la transferencia de datos */
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            close(listen_socks[i]);
            continue;

        } else if (pids[i] == 0) {
            /* --- PROCESO HIJO --- */
            close(s); /* El hijo no usa el socket de control */
            int s_data = accept(listen_socks[i], NULL, NULL);
            close(listen_socks[i]); /* Cierra el socket de escucha */
            
            if (s_data < 0) {
                perror("accept");
                exit(1);
            }

            FILE *fp = fopen(files[i], "wb");
            if (fp == NULL) {
                perror("fopen (hijo mget)");
                close(s_data);
                exit(1);
            }

            while ((n = recv(s_data, data, LINELEN, 0)) > 0) {
                fwrite(data, 1, n, fp);
            }
            fclose(fp);
            close(s_data);
            printf("Transferencia (GET) de %s completada (hijo %d).\n", files[i], getpid());
            exit(0);
            /* --- FIN PROCESO HIJO --- */
        }
        
        /* --- PROCESO PADRE --- */
        /* El padre cierra su copia del socket de escucha */
        close(listen_socks[i]); 

        /*
         * El padre AHORA lee la respuesta '226' de esta transferencia
         * ANTES de iterar para enviar el siguiente 'PORT'.
         * Esto sincroniza el canal de control.
         */
        n = read(s, res, LINELEN);
        if (n < 0) errexit("Error al leer respuesta 226: %s\n", strerror(errno));
        res[n] = '\0';
        printf("%s", res); // Imprime el "226 Transfer complete"
    }

    /* * BUCLE 2: Limpieza (Cleanup).
     * Ahora esperamos a que todos los hijos (que se ejecutaban 
     * en paralelo) terminen.
     */
    int completed_transfers = 0;
    for (i = 0; i < num_files; i++) {
        if (pids[i] > 0) {
            waitpid(pids[i], &status, 0);
            completed_transfers++;
        }
    }
    printf("mget: %d transferencias concurrentes manejadas.\n", completed_transfers);
}

/* Maneja el comando mput (transferencias concurrentes) */
void handle_mput(int s, char *linea) {
    char *files[MAX_FILES];
    int num_files = 0;
    char *token;
    char cmd[LINELEN], res[LINELEN], data[LINELEN];
    char local_ip_str[64];
    pid_t pids[MAX_FILES];
    int listen_socks[MAX_FILES];
    int i, n, status;
    FILE *fp;

    /* Parsear los nombres de archivo */
    token = strtok(linea, " "); // "mput"
    while ((token = strtok(NULL, " ")) != NULL && num_files < MAX_FILES) {
        files[num_files] = token;
        pids[num_files] = -1; // Inicializar
        listen_socks[num_files] = -1; // Inicializar
        num_files++;
    }

    if (num_files == 0) {
        printf("Uso: mput <archivo1> <archivo2> ...\n");
        return;
    }

    /* Obtener IP local para PORT */
    get_local_ip_for_port(s, local_ip_str);

    /* * BUCLE 1: Iniciar transferencias y leer respuestas de control.
     */
    for (i = 0; i < num_files; i++) {
        /* Validar archivo local primero */
        fp = fopen(files[i], "rb");
        if (fp == NULL) {
            printf("Error al abrir archivo local %s: %s. Saltando...\n", files[i], strerror(errno));
            continue;
        }
        fclose(fp); // Se cierra, el hijo lo reabrirá

        /* Crear socket de escucha local */
        listen_socks[i] = create_listen_socket();
        int port = get_socket_port(listen_socks[i]);

        /* Enviar comando PORT */
        sprintf(cmd, "PORT %s,%d,%d", local_ip_str, port / 256, port % 256);
        sendCmd(s, cmd, res);
        if (strncmp(res, "200", 3) != 0) {
            printf("Error: Comando PORT falló para %s. Saltando...\n", files[i]);
            close(listen_socks[i]);
            continue;
        }

        /* Enviar comando STOR */
        sprintf(cmd, "STOR %s", files[i]);
        sendCmd(s, cmd, res);
        if (strncmp(res, "150", 3) != 0) {
            printf("Error: Comando STOR falló para %s. Saltando...\n", files[i]);
            close(listen_socks[i]);
            // Leer cualquier otra respuesta para limpiar el buffer
            n = read(s, res, LINELEN);
            if (n > 0) res[n] = '\0';
            printf("%s", res);
            continue;
        }

        /* fork() para manejar la transferencia de datos */
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            close(listen_socks[i]);
            continue;

        } else if (pids[i] == 0) {
            /* --- PROCESO HIJO --- */
            close(s); /* El hijo no usa el socket de control */
            int s_data = accept(listen_socks[i], NULL, NULL);
            close(listen_socks[i]); /* Cierra el socket de escucha */
            
            if (s_data < 0) {
                perror("accept");
                exit(1);
            }

            FILE *fp_hijo = fopen(files[i], "rb");
            if (fp_hijo == NULL) {
                perror("fopen (hijo mput)");
                close(s_data);
                exit(1);
            }

            while ((n = fread(data, 1, LINELEN, fp_hijo)) > 0) {
                if (send(s_data, data, n, 0) < 0) {
                    perror("send (hijo mput)");
                    break;
                }
            }
            fclose(fp_hijo);
            close(s_data);
            printf("Transferencia (PUT) de %s completada (hijo %d).\n", files[i], getpid());
            exit(0);
            /* --- FIN PROCESO HIJO --- */
        }
        
        /* --- PROCESO PADRE ---*/
        /* El padre cierra su copia del socket de escucha */
        close(listen_socks[i]); 
        n = read(s, res, LINELEN);
        if (n < 0) errexit("Error al leer respuesta 226: %s\n", strerror(errno));
        res[n] = '\0';
        printf("%s", res); // Imprime el "226 Transfer complete"
    }

    /* * BUCLE 2: Limpieza (Cleanup).
     * Esperamos a todos los hijos.
     */
    int completed_transfers = 0;
    for (i = 0; i < num_files; i++) {
        if (pids[i] > 0) {
            waitpid(pids[i], &status, 0);
            completed_transfers++;
        }
    }
    printf("mput: %d transferencias concurrentes manejadas.\n", completed_transfers);
}


void ayuda () {
  printf ("Cliente FTP Concurrente. Comandos disponibles:\n \
    help                  - Despliega este texto\n \
    list [pat]            - LIST [pat] (formato largo del directorio)\n \
    cd <dir>              - CWD dir (cambiar directorio)\n \
    size <archivo>        - SIZE archivo (tamañ)\n \
    mkd <dir>             - MKD dir (crear directorio)\n \
    dele <archivo>        - DELE archivo (borrar archivo)\n \
    get <archivo>         - Copia el archivo desde el servidor al cliente (modo PASV)\n \
    put <file>            - Copia el archivo desde el cliente al servidor (modo PASV)\n \
    pput <file>           - Copia el archivo desde el cliente al servidor (modo PORT)\n \
    mget <r1> <r2> ...    - Varios GET concurrentes (modo PORT)\n \
    mput <l1> <l2> ...    - Varios PUT concurrentes (modo PORT)\n \
    quit                  - Finaliza la sesion FTP\n\n");
}


int main(int argc, char *argv[]) {
  char  *host = "localhost";  /* host to use if none supplied  */
  char  *service = "ftp";     /* default service name    */
  char  cmd[LINELEN], res[LINELEN];   /* buffer for cmds and replys from svr */
  char  data[LINELEN+1];      /* buffer to receive dirs (LIST) and send/recv files */
  char  user[32], *pass, prompt_line[LINELEN], *ucmd, *arg;
  int   s, s1=0, sdata, n;           /* socket descriptors, read count*/
  FILE  *fp;
  struct  sockaddr_in addrSvr;
  unsigned int alen = sizeof(addrSvr);
  char *ip_port_pput = NULL; // Para pput

  switch (argc) {
  case 1:
    host = "localhost";
    break;
  case 3:
    service = argv[2];
    /* FALL THROUGH */
  case 2:
    host = argv[1];
    break;
  default:
    fprintf(stderr, "Uso: %s [host [port]]\n", argv[0]);
    exit(1);
  }

  s = connectTCP(host, service);

  n = read (s, res, LINELEN);		/* lee msg del SVR, despues de conexion */
  if (n < 0) errexit("Error al leer saludo del servidor: %s\n", strerror(errno));
  res[n] = '\0';
  printf ("%s", res);

  while (1) {
    printf ("Please enter your username: ");
    if (scanf ("%31s", user) != 1) exit(1);
    sprintf (cmd, "USER %s", user); 
    sendCmd(s, cmd, res);
  
    pass = getpass("Enter your password: ");
    sprintf (cmd, "PASS %s", pass); 
    sendCmd(s, cmd, res);
    if (strncmp(res, "230", 3) == 0) break; // 230 Login successful
    printf("Error de login, intente de nuevo.\n");
  }

  /* Limpiar el buffer de entrada (el newline de getpass/scanf) */
  while (getchar() != '\n' && getchar() != EOF); 

  ayuda();

  while (1) {
    printf ("ftp> ");
    if (fgets(prompt_line, sizeof(prompt_line), stdin) != NULL) {
      prompt_line[strcspn(prompt_line, "\n")] = 0;
      
      /* Copia de la línea para mget/mput, ya que strtok la modifica */
      char linea_completa[LINELEN];
      strncpy(linea_completa, prompt_line, LINELEN);

      ucmd = strtok (prompt_line, " ");
      if (ucmd == NULL) continue;
  
      if (strcmp(ucmd, "list") == 0) {
        arg = strtok (NULL, ""); /* Captura el resto de la línea */
        sdata = pasivo(s);
        if (sdata < 0) continue;

        if (arg == NULL) {
            sprintf (cmd, "LIST"); 
        } else {
            sprintf (cmd, "LIST %s", arg);
        }
        sendCmd(s, cmd, res);
        
        if (strncmp(res, "150", 3) == 0) {
            while ((n = recv(sdata, data, LINELEN, 0)) > 0) {
              fwrite(data, 1, n, stdout);
            }
            close(sdata);
            n = read (s, res, LINELEN);
            if (n < 0) errexit("Error al leer respuesta post-lista: %s\n", strerror(errno));
            res[n] = '\0';
            printf ("%s", res);
        } else {
            close(sdata);
        }

      } else if (strcmp(ucmd, "get") == 0) {
  	    arg = strtok (NULL, " ");
        if (arg == NULL) { printf("Uso: get <archivo>\n"); continue; }
        sdata = pasivo(s);
        if (sdata < 0) continue;
        
        sprintf (cmd, "RETR %s", arg); 
        sendCmd(s, cmd, res);
        
        if (strncmp(res, "150", 3) == 0) { // 150 File status okay
            fp = fopen(arg, "wb");
            if (fp == NULL) {
                perror("fopen");
                close(sdata);
                continue;
            }
            while ((n = recv(sdata, data, LINELEN, 0)) > 0) {
              fwrite(data, 1, n, fp);
            }
            fclose(fp);
            close(sdata);
            n = read (s, res, LINELEN);
            res[n] = '\0';
            printf ("%s", res);
        } else {
            close(sdata);
        }
  
      } else if (strcmp(ucmd, "put") == 0) {
  	    arg = strtok (NULL, " ");
        if (arg == NULL) { printf("Uso: put <archivo>\n"); continue; }
        
        fp = fopen(arg, "rb");
	    if (fp == NULL) {perror ("Error al abrir archivo local"); continue;}
        
        sdata = pasivo(s);
        if (sdata < 0) { fclose(fp); continue; }
        
        sprintf (cmd, "STOR %s", arg);
        sendCmd(s, cmd, res);

        if (strncmp(res, "150", 3) == 0) {
            while ((n = fread (data, 1, LINELEN, fp)) > 0) {
              if (send (sdata, data, n, 0) < 0) {
                  perror("send");
                  break;
              }
            }
            fclose(fp);
            close(sdata);
            n = read (s, res, LINELEN);
            res[n] = '\0';
            printf ("%s", res);
        } else {
            fclose(fp);
            close(sdata);
        }

      } else if (strcmp(ucmd, "pput") == 0) {
  	    arg = strtok (NULL, " ");
        if (arg == NULL) { printf("Uso: pput <archivo>\n"); continue; }

        fp = fopen(arg, "rb");
	    if (fp == NULL) {perror ("Error al abrir archivo local"); continue;}
       
	    if (s1==0) { /* Primera vez, crear socket de escucha y obtener IP */
            char ip_str_fmt[64];
            s1 = create_listen_socket(); // Usa la nueva función
            int port_pput = get_socket_port(s1);
            get_local_ip_for_port(s, ip_str_fmt);

            /* Guardar IP,puerto formateado */
            if (ip_port_pput) free(ip_port_pput); // Liberar si existía
            ip_port_pput = (char*) malloc(128);
            snprintf(ip_port_pput, 128, "%s,%d,%d", ip_str_fmt, port_pput / 256, port_pput % 256);
	    }

        sprintf (cmd, "PORT %s", ip_port_pput);
        sendCmd(s, cmd, res);
        if (strncmp(res, "200", 3) != 0) { fclose(fp); continue; }
        
        sprintf (cmd, "STOR %s", arg); 
        sendCmd(s, cmd, res);
        if (strncmp(res, "150", 3) != 0) { fclose(fp); continue; }

        sdata = accept(s1, (struct sockaddr *)&addrSvr, &alen);
        if (sdata < 0) { perror("accept pput"); fclose(fp); continue; }
        
        while ((n = fread (data, 1, LINELEN, fp)) > 0) {
          if (send (sdata, data, n, 0) < 0) {
              perror("send pput");
              break;
          }
        }
        fclose(fp);
        close(sdata);
        n = read (s, res, LINELEN);
        res[n] = '\0';
        printf ("%s", res);

      } else if (strcmp(ucmd, "cd") == 0) {
  	    arg = strtok (NULL, " ");
        if (arg == NULL) { printf("Uso: cd <directorio>\n"); continue; }
        sprintf (cmd, "CWD %s", arg); 
        sendCmd(s, cmd, res);

      } else if (strcmp(ucmd, "size") == 0) {
  	    arg = strtok (NULL, " ");
        if (arg == NULL) { printf("Uso: size <archivo>\n"); continue; }
        sprintf (cmd, "SIZE %s", arg); 
        sendCmd(s, cmd, res);

      } else if (strcmp(ucmd, "mkd") == 0) {
  	    arg = strtok (NULL, " ");
        if (arg == NULL) { printf("Uso: mkd <directorio>\n"); continue; }
        sprintf (cmd, "MKD %s", arg); 
        sendCmd(s, cmd, res);

      } else if (strcmp(ucmd, "dele") == 0) {
  	    arg = strtok (NULL, " ");
        if (arg == NULL) { printf("Uso: dele <archivo>\n"); continue; }
        sprintf (cmd, "DELE %s", arg); 
        sendCmd(s, cmd, res);

      } else if (strcmp(ucmd, "mget") == 0) {
        handle_mget(s, linea_completa);

      } else if (strcmp(ucmd, "mput") == 0) {
        handle_mput(s, linea_completa);

      } else if (strcmp(ucmd, "quit") == 0) {
        sprintf (cmd, "QUIT"); 
        sendCmd(s, cmd, res);
        close(s);
        if (ip_port_pput) free(ip_port_pput);
	    exit (0);

      } else if (strcmp(ucmd, "help") == 0) {
	    ayuda();

      } else {
        printf("%s: comando no implementado.\n", ucmd);
      }
    } else {
      /* EOF (Ctrl+D) */
      printf("\nSaliendo (EOF)...\n");
      sprintf (cmd, "QUIT"); 
      sendCmd(s, cmd, res);
      close(s);
      if (ip_port_pput) free(ip_port_pput);
      exit(0);
    }
  }
  return 0;
}