# Cliente FTP Concurrente

Este es un cliente FTP basado en C, desarrollado como un proyecto para implementar funcionalidades avanzadas del protocolo FTP (RFC 959), con un enfoque en la transferencia de archivos concurrente.

# Configuraciones

### Servidor

Este cliente utiliza dos modos de transferencia, lo que requiere una configuración específica en el servidor vsftpd para evitar bloqueos.

### Modo Pasivo (PASV)
* **Usado por:** `get` y `put`.
* **Cómo funciona:** El cliente le pide al servidor que abra un puerto y el cliente se conecta a él.

### Modo Activo (PORT)
* **Usado por:** `mget`, `mput`, `pput`.
* **Cómo funciona:** El cliente abre un puerto y le pide al servidor que se conecte a él.

### Archivo vsftpd (/etc/vsftpd.conf)
```bash
listen=YES
local_enable=YES
write_enable=YES
dirmessage_enable=YES
use_localtime=YES
xferlog_enable=YES
pasv_enable=YES
port_enable=YES
connect_from_port_20=YES
chroot_local_user=YES
allow_writeable_chroot=YES
```
### Cambio minimo en connectsock.c

Mapeo del nombre del servicio:

```bash
if ( (pse = getservbyname(service, transport)) )
```
Mapeo del nombre del host:
```bash
if ( (phe = gethostbyname(host)) )
```

## Características Implementadas

El cliente soporta los siguientes comandos FTP:

* **`list [pat]`**: Muestra el listado del directorio actual (o de `[pat]`).
* **`cd <dir>`**: Cambia el directorio de trabajo en el servidor.
* **`size <archivo>`**: Muestra el tamaño de un archivo en el servidor.
* **`mkd <dir>`**: Crea un nuevo directorio en el servidor.
* **`dele <archivo>`**: Borra un archivo en el servidor.
* **`get <archivo>`**: Descarga un solo archivo usando el modo **Pasivo (PASV)**.
* **`put <file>`**: Sube un solo archivo usando el modo **Pasivo (PASV)**.
* **`pput <file>`**: Sube un solo archivo usando el modo **Activo (PORT)**.
* **`mget <r1> [r2] ...`**: Descarga múltiples archivos **concurrentemente** usando el modo **Activo (PORT)** y `fork()`.
* **`mput <l1> [l2] ...`**: Sube múltiples archivos **concurrentemente** usando el modo **Activo (PORT)** y `fork()`.
* **`help`**: Muestra el menú de ayuda.
* **`quit`**: Termina la sesión y sale del cliente.

## Compilación

Para compilar el proyecto, se asume que los siguientes archivos `.c` están en el mismo directorio:

* `ReinosoA-clienteFTP.c` (El archivo principal)
* `connectTCP.c` (Librería de ayuda para conectar)
* `connectsock.c` (Librería de ayuda para sockets)
* `errexit.c` (Librería de ayuda para manejo de errores)

Simplemente se ejecuta el siguiente comando para utilizar el `Makefile` proporcionado:

```bash
make -f Makefile-ClienteFTP
```

# Uso
Ejecuta el cliente sin especificar o especificando el host y, opcionalmente, el puerto:

```bash
./clienteFTP
```
o
```bash
./clienteFTP [host [puerto]]
```

# Verificación de Concurrencia

Para visualizar en tiempo real la creación y ejecución simultánea de los procesos hijos (utilizando herramientas como `ps`, `htop` o `watch`), se recomienda realizar las pruebas de los comandos `mget` y `mput` con archivos de tamaño considerable (al menos **50 MB** cada uno).

En pruebas locales (`localhost`), la velocidad de transferencia es extremadamente alta. Si se utilizan archivos pequeños (KB o pocos MB), los procesos hijos completan su tarea y terminan casi instantáneamente, haciendo imperceptible su ejecución paralela ante la observación humana.
### Ejemplo de Sesión (Concurrente)
```bash
$ ./clienteFTP localhost
220 (vsFTPd 3.0.5)
Please enter your username: anthony
331 Please specify the password.
Enter your password:
230 Login successful.
... (menú de ayuda) ...

ftp> mget prueba.txt prueba2.txt
200 PORT command successful. Consider using PASV.
150 Opening BINARY mode data connection for prueba.txt (...)
226 Transfer complete.
200 PORT command successful. Consider using PASV.
150 Opening BINARY mode data connection for prueba2.txt (...)
Transferencia (GET) de prueba.txt completada (hijo 23617).
226 Transfer complete.
Transferencia (GET) de prueba2.txt completada (hijo 23621).
mget: 2 transferencias concurrentes manejadas.

ftp> quit
221 Goodbye.
```

