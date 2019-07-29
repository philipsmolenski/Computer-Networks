# Distrubuted file system

## Description
The aplication can be used to store files in multiple server nodes. Clients can serach, change content or remove files.

## How to compile and run
To generate executable files type:

```sh
make
```

To run server node type:

```sh
./netstore-server -g MCAST_ADDR -p CMD_PORT -b MAX_SPACE -f SHRD_FLDR -t TIMEOUT

```
where:

MCAST_ADDR is the multicast address of the server node
CMD_PORT is the UDP port used to send and receive commands from client
MAX_SPACE is the maximal space provided by this node in bytes, default value is 52428800
SHRD_FLDR is a path to a folder in which files will be stored by this node
TIMEOUT is the time in seconds during which the server is waiting for client nodes to answer after a query has been send.

To run client node type:

```sh
./netstore-client -g MCAST_ADDR -p CMD_PORT -o OUT_FLDR -t TIMEOUT

```

where:

MCAST_ADDR is the multicast address of the client node
CMD_PORT is the UDP port used to send and receive commands from server
OUT_FLDR is the path to a folder in which files downloaded from server are saved
TIMEOUT is the time in seconds during which the clinet is waiting for server nodes to answer after a query has been send

## How to use
Each of client nodes provides the following operations through the command line:

discover - find and print all available server nodes

search %s - search for files that contain the sequence %s in their names (works also if %s is the empty string and finds all available files)

fetch %s - download file named %s

remove %s - remove file named %s from each of server nodes

exit - finish working


## Possible improvements
The text below (in Polish) describes an idea to improve the program in such a way that each file is stored in only one server node (i.e. clients cannot upload the same file to two different server nodes)

Rozwiązanie części C zadania 2. opierać się będzie o algorytmy znane z wykładów programowania współbieżnego: algrotym Ricarta-Agrawali oraz algorytm Lamporta synchronizacji zegarów logicznych. Każdy węzeł serwerowy będzie przechowywał:

-zmienną całkowitoliczbową reprezentującą jego zegar logiczny (ustawioną początkowo na zero)

-kolejkę serwerów (początkowo pustą), od których otrzymał prośbę o wejście do sekcji krytycznej 

-listę pozwoleń na wejście do sekcji krytycznej

-listę żądań, które wysłał od momentu, gdy zaczął czekać na sekcję krytyczną, wraz z wartością jego zegara logicznego w momencie wysyłania prośby 

-listę serwerów, które są aktualnie aktywne (wyłączając siebie).

-swoją nazwę będącą losowo wygenerowanym ciągiem 100 znaków. 

Na wejście do sekcji krytycznej czekają te węzły, które planują dodać pliki do puli plików przechowywanych przez grupę w (a zatem są to węzły dołączające do grupy serwerów oraz te, które dostały od klienta prośbę o dodanie pliku). Protokół postępowania węzła serwerowego w części C zostaje rozszerzony o następujące komunikaty:

1. Po dołączeniu do grupy węzłów serwerowych, węzeł W wysyła na adres MCAST_ADDR pakiet SIMPL_CMD z poleceniem cmd = "HEJ" oraz pustym polem data. Pozostałe serwery po otrzymaniu takiego pakietu wysyłają na adres jednostkowy serwera W wiadomość SIMPL_CMD z cmd = "SIEMA" oraz pustym polem data. Wówczas węzeł W dodaje adres nadawcy wiadomości "SIEMA" do swojej listy przechowującej aktywne węzły. Węzeł W na odpowiedzi "SIEMA" czeka TIMEOUT sekund.

2. Po otrzymaniu wiadomości "HEJ" węzeł W traktuje ją jak wiadomość CMPLX_CMD z cmd = "CZEKAM" oraz param = 0, postępowanie w takim przypadku opisane jest niżej. Ponadto dodaje on adresata wiadomości do listy aktywnych węzłów.

3. Po otrzymaniu wiadomości "ADD" od klienta węzeł W, jeżeli czekał już na wejście do sekcji krytycznej to zapamiętuje tylko dane otrzymane z wiadomości klienta i czeka dalej. W przeciwnym wypadku rozsyła on na adres MCAST_ADDR wiadomość CMPLX_CMD  cmd = "CZEKAM" oraz wartością swojego zegara logicznego w polu param oraz swoją nazwą w polu data.

4. Po otrzymaniu wiadomości "CZEKAM" od serwera V z param = t1 węzeł W, jeżeli nie czeka na wejście do sekcji krytycznej, to odpowiada na adres unicast nadawcy wiadomością SIMPL_CMD z cmd = "RUSZAJ" oraz pustym polem data. W przeciwnym wypadku sprawdza on, czy czas t2 wysłania jego prośby o dołączenie do sekcji krytycznej jest większy, niż t1. Jeżeli t1 < t2 to W wysyła wiadomość SIMPL_CMD z cmd = "RUSZAJ" (jeżeli t1 = t2, to robi to wtedy i tylko wtedy, gdy jego nazwa jest leksykograficznie mniejsza od nazwy V przesłanej w polu data). W przeciwnym przypadku W dodaje V do kolejki węzłów oczekujących na pozwolenie. Następnie, niezależnie od tego, który z powyższych przypadków zaszedł, węzeł W ustawia swój zegar logiczny na wartość max (t1, t2) + 1.

5. Węzeł W po otrzymaniu wiadomości "RUSZAJ" dodaje adres nadawcy do listy otrzymanych pozwoleń (chyba że dostał już potwierdzenie z tego samego adresu, wtedy go nie dodaje) i sprawdza, czy  jej długość jest równa długości listy aktywnych węzłów serwerowych. Jeżeli tak, to wchodzi on do sekcji krytycznej, w przeciwnym wypadku nic nie robi.

6. Po wyjściu z sekcji krytycznej, węzeł W czyści listę otrzymanych pozwoleń oraz swoich próśb o wejście do sekcji krytycznej. Następnie opróżnia kolejkę węzłów, które poprosiły o wejście do sekcji krytycznej, wysyłając do każdego z nich na adres unicast wiadomość SIMPL_CMD w cmd = "RUSZAJ" oraz pustym polem data.

7. W momencie wejścia do sekcji krytycznej, węzeł W dla każdego pliku, który chce aktualnie dodać, wysyła wiadomość SIMPL_CMD z cmd = "MASZ_PLIK?" oraz polem data wypełnionym nazwą pliku, który ma być dodany na serwer. Następnie oczekuje od pozostałych aktywnych węzłów odpowiedzi CMD_SIMPL z cmd = "MAM" lub cmd = "NIE_MAM" oraz polem data wypełnionym nazwą pliku. Jeżeli węzeł otrzyma od któregokolwiek węzła odpowiedź "MAM", to rezygnuje z dodania owego pliku (odpowiadając wiadomością "NO_WAY" klientowi, który chciał dodać ten plik, jeżeli takowy istnieje), w przeciwnym wypadku dodaje plik na serwer. Jeżeli w trakcie pobytu w sekcji krytycznej węzeł W dostanie prośbę od klienta o dodanie kolejnego pliku na serwer, nie realizuje on jego prośby, wysyłając wiadomość "MASZ_PLIK?" do innych serwerów (takie zachowanie mogłoby spowodować zagłodzenie pozostałych węzłów w grupie). Zamiast tego zapamiętuje on prośbę klienta i zgłasza chęć wejścia do sekcji krytycznej od razu po jej opuszczeniu wysyłając wiadomość "CZEKAM".

8. Gdy węzeł dostaje wiadomość "MASZ_PLIK?" to sprawdza wszysktie nazwy plików, które aktualnie przechowuje lub dodaje na serwer. Jeżeli nazwa któregoś z plików pokrywa się z nazwą w polu data, to odpowiada na adres unicast nadawcy wiadomością SIMPL_CMD z cmd = "MAM" oraz nazwą pliku w polu data. W przeciwnym wypadku wysyła wiadomość SIMPL_CMD z cmd = "NIE_MAM" oraz nazwą pliku w polu data.

9. Gdy węzeł opuszcza grupę, wysyła na adres MCAST_ADDR wiadomość SIMPL_CMD z cmd = "PAPA" oraz pustym polem data.

10. Gdy węzeł W dostaje wiadomość "PAPA", traktuje ją jak wiadomość "RUSZAJ", a ponadto wyrzuca nadawcę z listy aktywnych węzłów.

Synchronizacja zegarów metodą Lamporta gwarantuje, że w sekcji krytycznej znadzie się w każdej chwili maksymalnie 1 węzeł. To z kolei gwarantuje, że węzeł znajdujący się w sekcji krytycznej zawsze otrzyma aktualną informację na temat plików znajdujących się oraz dodawanych na serwer. Dzięki temu doda on plik tylko wtedy, gdy w grupie węzłów nie znadjuje się żaden inny plik o tej samej nazwie, co gwarantuje spełnienie wymagań części C.

