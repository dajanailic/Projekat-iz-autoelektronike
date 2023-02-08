Ideja i zadaci:
1. Pratiti stanje senzora. Posmatrati vrednosti koje se dobijaju iz UniCom softvera.
Trigerovati automatski odgovor UniCom-a traženom periodom, a odgovor upisati u R1 polje,
i menjati ga po potrebi. 
2. Realizovati komunikaciju PC-ja sa simuliranim sistemom. Slati naredbe preko simulirane
serijske komunikacije. 
Naredbe su:
a. Biranje moda sistema preko serijske komunikacije:
- MONITOR prikazivanje senzora koji govore da li sistem ispravno radi
- DRIVE prikazivati senzore na osnovu kojih se može optimizovati vožnja po potrošnji
- SPEED prikazivanje senzora na osnovu kojih se može optimizovati vožnja po brzini

b. Pored komandi sa serijske, omogućiti uključivanje sistema preko prekidača, u tu svrhu
pratiti stanje 3 najniže LEDovke na LED baru (podesiti 1 stubac kao ulazni, a drugi kao
izlazni) koji će simulirati prekidače, tj. kad je pritisnut prekidač (dioda uključena) potrebno je
prebaciti se u određeni mod.

Kada je sistem uključen potrebno je jednu izlaznu diodu na LED baru koristiti kao
LED_sistem_aktivan znak.

Ako je sistem u kritičnom stanju, u kojem god modu da se nalazimo, potrebno je da sve
izlazne diode blinkaju periodom od 200ms.

3. Na LED BAR programu prikazati trenutno stanje senzora koji se odnose na određeni mod
1. MONITOR: Senzor temperature rashladne tečnosti i senzor temperature vazduha
usisne grane
2. DRIVE: Senzor obrtaja i senzor opterećenja motora
3.  SPEED: Senzor obrtaja i senzor pedale gasa
Na 7Seg displeju ispisati mod rada kako bi se znalo šta analogni pokazivači pokazuju.



Periferije:
Periferije koje smo koristili su:
AdvUniCom.exe, LED_bars_plus.exe I Seg7_Mux.exe. 
Prilikom pokretanja AdvUniCom.exe potrebno je pokrenuti kanal 0 I kanal 1.
Za sedmosegmentni displej prilikom pokretanja periferije dodati argument 10. 
Pokretanjem LED_bars.exe gR  dobićemo dva LED stubca gdje prvi predstavlja tastere za uključivanje željenog moda a drugi predstavlja diode koje se uključuju zavisno od moda. 



Kratak pregled taskova

Glavni .c fajl ovog projekta je main_application.cvoid 

Prijem_sa_senzora(void* pvParameters) //
U ovom tasku vrši se obrada podataka sa senzora kao si slanje poruke da li je potrebno ukljuciti alarm.

void led_bar_tsk(void* pvParameters) //
Task koji očitava koji je taster pritisnut na LED bar-u I na osnovu toga čuva trenutno stanje.

void Seg7_ispis_task(void* pvParameters)//
Ispisuje na 7seg displej informacije (1 za MONITOR, a 2 za DRIVE I 3 za SPEED), u zavisnosti koji mod je prikazan na displeju, mozemo da vidimo I vrednosti sa senzora.

void AlarmTask(void* pvParameters)//
Task koji aktivira alarm ukoliko je neka vrednost veća od dozvoljene.

void PC_command(void* pvParameters)//
Task koji proverava koja naredba je stigla sa kanala 1 I na osnovu toga određuje mod I čuva to stanje.

void PC_SerialReceive_Task(void* pvParameters)//
Ovaj task sluzi sa primanje podataka sa PC-a.


void SerialReceive_Task(void* pvParameters)//
Task koji služi sa prijem podataka koji stižu sa kanala 0.

void SerialSend_Task(void* pvParameters)//
Ovima taskom simuliramo vrednosti koje stižu sa senzora na svakih 1s, tako što šaljemo karaktere XYZ.

Predlog simuliranja sistema:

Pokrenuti sve periferije na način na koji je objašnjeno. Zatim pokrenuti program.
U prozoru kanala 0 u polje R1 unijeti vrijednosti npr. 00\v1\v2\v3\v4\v5\v6\ff. v2 predstavlja vrijednost temperature, a vrijednosti obrtaja su na poljima v3 i v4. 
Zatim u polje koje je oznaceno sa T1 unijeti XYZ i čekirati "Auto".
Mod možemo birati ili preko led bara ili preko kanala 1.
Ako želimo da izaberemo mod MONITOR, pritisnemo treću diodu od dole u prvoj koloni (kolona je zelene boje), na displeju će biti prikazan broj 1 na početku za oznaku modu kao i vrednosti sa senzora koje taj mod prikazuje.
Ako želimo da to bude mod DRIVE, pritisnemo drugu od dole.
Za mod SPEED pritisnemo prvu od dole.
U slučaju da hoćemo da izaberemo mod preko kanala 1, u tom slučaju šaljemo ili MONITOR\0d ili DRIVE\0d ili SPEED\0d.

Svaki put kada izaberemo neki mod, u drugoj koloni u led baru (crvene diode) prva od dole dioda će da sija.
U slučaju alarma koji se uključuje kada neka vrednost prelazi granicu, diode u drugoj koloni će se sve ukljuciti.

