// Gestion de l'arosage automatique
// arosage palaiseau jardin du bas
// Eric dupuy
// ericdelpozo@free.fr


#include <RTClib.h>
#include <Wire.h>

#define nb_vannes 3               // il y a 3 vannes programmables + 1 qui sert de coupure générale (sécurité)
#define nb_programmes 5
#define duree_max 3600000         //durée maximum en continu
#define pin_bypass 2              //pin du bouton pour forcer un cycle d'arrosage
#define pin_horloge 3             // pin du bouton qui réinitialise l'horloge  (en cas de changement de pile)
#define pin_vanne 4               //pour le premier relai de l'électrovanne , pour les autres on ajoute 1, 
#define pin_selecteur_programme 9 // 9, 10, 11 12 (4 positions)
#define debug_serial 0            //1 --> sorties sérial de debug, sinon, rien
#define duree_cycle_force 300     // durée en seconde d'un cycle d'allumage forcé
struct temps
{
  unsigned char heure;
  unsigned char minute;
  unsigned char seconde;
};
struct Trigg                // un déclancheur = plage de fonctionnement
{
  struct temps debut;
  struct temps fin;
};
struct Prog                   //structure d'une programmation pour l'ensemble des vannes
{
  struct Trigg* vannes[nb_vannes];
  unsigned char nb_triggs[nb_vannes];
};

bool flag_clock_reset=false;
bool bypass=false;            //cycle d'arrosage focré qui prend temporairement la main sur la programmation
long start_action=0;
bool started_action=false;
RTC_DS3231 rtc;
struct temps start_bp={0,0,0};
struct temps end_bp={0,0,0};
struct temps actuel_temps;    // heure courante (mise à jour dans le loop)
struct Prog* programmes;      //ensemble des programmes


//calcul de l'écart en secondes,  avec t2 représentant une date postérieure à t1 (entre 0h00 et 23h59)
long ecart_temps(struct temps* t1,struct temps * t2)
{
  long d1=(long)t1->heure*3600L+(long)t1->minute*60L+(long)t1->seconde;
  long d2=(long)t2->heure*3600L+(long)t2->minute*60L+(long)t2->seconde;
  if (d2>d1) {return d2-d1;} else {return 86400-d1+d2;}
}

// teste si val appartient à un intervalle entre deux temps
bool temps_in(struct temps* start,struct temps* end,struct temps* val)
{
return (ecart_temps(start,val)<= ecart_temps(start,end));
}

//recupère l'heure courante
void get_temps(struct temps* t)
{
  DateTime now = rtc.now();
  t->heure=now.hour();
  t->minute=now.minute();
  t->seconde=now.second();
}

//affiche les informations au démarrage, en mode debug
void info_setup()
{
#if debug_serial
  Serial.print("\nProgrammation de l'arrosage automatique\n");
  Serial.print(nb_vannes);
  Serial.print(" vannes disponibles\n\n");
  Serial.print(nb_programmes -1);
  Serial.print(" programmes disponibles\n\n");
  Serial.print("temps continue maximal (protection) :");
  Serial.print((long)duree_max);
  Serial.print("\n\n");
  Serial.print("\n programmes disponibles:\n");
    for (int i=0;i<nb_programmes;i++){
      info_prg(i);
    }
#endif
}
void setup(){

  Serial.begin(9600);
  Wire.begin();
  rtc.begin();

  // déclaration des deux intérruptions attachées aux boutons
  pinMode(pin_bypass, INPUT_PULLUP);
  pinMode(pin_horloge, INPUT_PULLUP);

  //initialisation des pins du GPIO
  pinMode( pin_selecteur_programme, INPUT_PULLUP);
  pinMode( pin_selecteur_programme+1, INPUT_PULLUP);
  pinMode( pin_selecteur_programme+2, INPUT_PULLUP);
  pinMode( pin_selecteur_programme+3, INPUT_PULLUP);
  pinMode(pin_vanne, OUTPUT);
  pinMode(pin_vanne+1, OUTPUT);
  pinMode(pin_vanne+2, OUTPUT);
  pinMode(pin_vanne+3, OUTPUT);
  
  digitalWrite(pin_vanne, HIGH);    // on ferme mes relais
  digitalWrite(pin_vanne+1, HIGH); 
  digitalWrite(pin_vanne+2, HIGH);
  digitalWrite(pin_vanne+3, HIGH);

//déclaration des programmes des plages horaires de toutes les vannes
programmes=(struct Prog* )malloc(sizeof(struct Prog)*nb_programmes);
//Le programme 0 est utilsé pour le cycle d'arrosage forcé, ne pas l'utiliser
programmes[0].nb_triggs[0]=1;
programmes[0].nb_triggs[1]=1;
programmes[0].nb_triggs[2]=1;
programmes[0].vannes[0] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[0].nb_triggs[0]);
programmes[0].vannes[1] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[0].nb_triggs[1]);
programmes[0].vannes[2] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[0].nb_triggs[2]);
programmes[0].vannes[0][0]={0,0,0,0,0,0};
programmes[0].vannes[1][0]={0,0,0,0,0,0};
programmes[0].vannes[2][0]={0,0,0,0,0,0};

//programme1 soir et matin 10 mins
programmes[1].nb_triggs[0]=2;                 // 2 déclanchements dans la journée pour chaque vanne
programmes[1].nb_triggs[1]=2;
programmes[1].nb_triggs[2]=2;
programmes[1].vannes[0] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[1].nb_triggs[0]);
programmes[1].vannes[1] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[1].nb_triggs[1]);
programmes[1].vannes[2] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[1].nb_triggs[2]);
programmes[1].vannes[0][0]={6 ,0,0,6 ,10,0};   // premier declencheur de la première vanne 
programmes[1].vannes[0][1]={20,0,0,20,10,0};   // deuxième déclencheur de la première vanne
programmes[1].vannes[1][0]={6 ,10,1,6 ,20,0};
programmes[1].vannes[1][1]={20,10,1,20,20,0};
programmes[1].vannes[2][0]={6 ,20,1,6 ,30,0};
programmes[1].vannes[2][1]={20,20,1,20,30,0};

//programme2 soir et matin 15 mins
programmes[2].nb_triggs[0]=2;
programmes[2].nb_triggs[1]=2;
programmes[2].nb_triggs[2]=2;
programmes[2].vannes[0] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[2].nb_triggs[0]);
programmes[2].vannes[1] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[2].nb_triggs[1]);
programmes[2].vannes[2] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[2].nb_triggs[2]);
programmes[2].vannes[0][0]={6 ,0,0,6 ,15,0};
programmes[2].vannes[0][1]={20,0,0,20,15,0};
programmes[2].vannes[1][0]={6 ,15,1,6 ,30,0};
programmes[2].vannes[1][1]={20,15,1,20,30,0};
programmes[2].vannes[2][0]={6 ,30,1,6 ,45,0};
programmes[2].vannes[2][1]={20,30,1,20,45,0};

//programme3 matin 15 mins
programmes[3].nb_triggs[0]=1;
programmes[3].nb_triggs[1]=1;
programmes[3].nb_triggs[2]=1;
programmes[3].vannes[0] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[3].nb_triggs[0]);
programmes[3].vannes[1] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[3].nb_triggs[1]);
programmes[3].vannes[2] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[3].nb_triggs[2]);
programmes[3].vannes[0][0]={6,0,0,6,15,0};
programmes[3].vannes[1][0]={6,15,1,6,30,0};
programmes[3].vannes[2][0]={6,30,1,6,45,0};
//programme4 soir 15 min
programmes[4].nb_triggs[0]=1;
programmes[4].nb_triggs[1]=1;
programmes[4].nb_triggs[2]=1;
programmes[4].vannes[0] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[4].nb_triggs[0]);
programmes[4].vannes[1] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[4].nb_triggs[1]);
programmes[4].vannes[2] =(struct Trigg* )malloc(sizeof(struct Trigg)*programmes[4].nb_triggs[2]);
programmes[4].vannes[0][0]={20,0,0,20,15,0};
programmes[4].vannes[1][0]={20,15,1,20,30,0};
programmes[4].vannes[2][0]={20,30,1,20,45,0};

info_setup();

Serial.print("\nÉtat: démarré\n");
start_action=0;
started_action=false;

}

// Afficher des plages d'un programme sur le port serie
void info_prg(unsigned char p)
{
#if debug_serial
  Serial.print("programme ");
  Serial.print(p,"\n");
 
  for (unsigned i=0;i<nb_vannes;i++)
  {
   for (unsigned j=0;j< programmes[p].nb_triggs[i];j++)
   {
     struct temps debut=programmes[p].vannes[i][j].debut;
     struct temps fin=programmes[p].vannes[i][j].fin;
     
      Serial.write("\nvanne");
      Serial.print(i+1);
      Serial.write("  ");      
      Serial.print(debut.heure);
      Serial.print(":");
      Serial.print(debut.minute);
      Serial.print(":");
      Serial.print(debut.seconde);
      Serial.write(" -->  ");
      Serial.print(fin.heure);
      Serial.print(":");
      Serial.print(fin.minute);
      Serial.print(":");
      Serial.print(fin.seconde);
     }
  }
#endif
}


void set_bypass()   //déclanché par un bouton, mise en place du cycle forcé
{
  unsigned char h=actuel_temps.heure;
  unsigned char m=actuel_temps.minute;
  unsigned char s=actuel_temps.seconde;

if (bypass==true){return;}   //attendre la fin du cycle forcé avant de pouvoir en relancer un autre
  
#if debug_serial
  Serial.print("\n Mise en route du Cycle forcé");
#endif   
  
  //calculs des plages horaires du programme 0 pour un cycle forcé d'allumage de vannes  

  start_bp={h,m,s};
  
  unsigned char h2,m2,s2; 
  long starts=(long)h*3600L+(long)m*60L+(long)s;
  long duree=duree_cycle_force ;              
  long ends=starts+duree;
    
  if (ends>86400) {ends=ends-86400;}
  h2=ends /3600;
  m2=ends %3600/60;  
  s2=ends %60;  
  
  programmes[0].vannes[0][0]={h,m,s,h2,m2,s2};
  
  ends=ends+1;
  if (ends>86400) {ends=ends-86400;}
  h=ends /3600;
  m=ends %3600/60;  
  s=ends %60;  
   ends=ends+duree;
  if (ends>86400) {ends=ends-86400;}
  h2=ends /3600;
  m2=ends %3600/60;  
  s2=ends %60;  
  programmes[0].vannes[1][0]={h,m,s,h2,m2,s2};
  ends=ends+1;
  if (ends>86400) {ends=ends-86400;}
  h=ends /3600;
  m=ends %3600/60;  
  s=ends %60;  
  ends=ends+duree;
  if (ends>86400) {ends=ends-86400;}
  h2=ends /3600;
  m2=ends %3600/60;  
  s2=ends %60;  
  
  programmes[0].vannes[2][0]={h,m,s,h2,m2,s2};

  ends=ends+1;
  if (ends>86400) {ends=ends-86400;}
  h=ends /3600;
  m=ends %3600/60;  
  s=ends %60;  
  end_bp={h,m,s};
#if debug_serial
  Serial.print("\n cycle forcé\n");
  info_prg(0);
#endif
  bypass=true;
  }


//recupère le programme en cours,  p=-1 si le selecteur est sur le position 0: programmation éteinte
int get_programme()
{
int p=-1;
if (digitalRead(pin_selecteur_programme)==0) {p=1;}
if (digitalRead(pin_selecteur_programme+1)==0) {p=2;}
if (digitalRead(pin_selecteur_programme+2)==0) {p=3;}
if (digitalRead(pin_selecteur_programme+3)==0) {p=3;}
return p;
}

// Boucle principale
void loop(){                
  int prg;
  bool in_action=false;
  bool force_stop=false;
  long duree_action=0;
  long ts1,ts2;
  
  ts1=millis();

  get_temps(&actuel_temps);   //lecture de l'heure courante
  
//#################################################
//#      détermiation du programme à traiter      #
//#################################################
  if ((bypass==true ) && !temps_in(&start_bp,&end_bp,&actuel_temps))
      {bypass=false;}        
  prg=(bypass==true)? 0:get_programme();    // en bypass c'est le programme 0 qui s'applique
   

//#######################################################
//#          Calcul de la durée de l'action en cours    #
//#######################################################
  if (started_action==false) {duree_action=0;}
  else{ 
    long d=millis();
    if (d>=start_action) {duree_action=d-start_action;}
    else{duree_action=(4294967295-start_action)+d;}
     }
  force_stop=(duree_action>duree_max);
   
//affichage d'infos de debuggage
#if debug_serial
  char tx[70];
  sprintf(tx, "\nTime %02d:%02d:%02d prog=%d started=%d durée=%ld force_stop=%d", actuel_temps.heure, actuel_temps.minute,actuel_temps.seconde,prg,started_action,duree_action,force_stop);  
  Serial.print(tx); 
#endif

//#################################################
//#        traitement de la programmation         #
//#################################################
  in_action=false;
   if (prg>=0)  //si prg=-1 --> le selecteur de probramme est sur la position 0--> aucun programme a effectuer
    {   
      //allumage des vannes selon le programme

      for (unsigned i=0;i<nb_vannes;i++)
      {
        bool etatvanne=false;
        for (unsigned j=0;j< programmes[prg].nb_triggs[i];j++)
        {
          struct temps debut=programmes[prg].vannes[i][j].debut;
          struct temps fin=programmes[prg].vannes[i][j].fin;
          if (temps_in(&debut,&fin,&actuel_temps)==true)   //regarde tous les déclancheur pour la vanne i
          {
            etatvanne=true;       // permet de savoir si la vanne testée est allumée par un des déclancheurs
            in_action=true;       // si false en sortie de boucles, aucune vanne n'est en cours d'utilisation pour l'eheure courante
          }
        }
        if (etatvanne && !force_stop)
          {
            digitalWrite(pin_vanne,     LOW);   // ouverture de la vanne principale et de la vanne en action
            digitalWrite(pin_vanne+1+i, LOW); 
            if (started_action==false) {         // initialise le compteur pour le calcul de la durée
#if debug_serial              
                Serial.print("\n Allumage vanne ");
                Serial.print(i+1);  
#endif
                started_action=true;
                start_action=millis();}             
          }
        else
          {         
#if debug_serial                       
          Serial.print("\n Extinction vanne ");
          Serial.print(i+1);
#endif      
          
          digitalWrite(pin_vanne+1+i,HIGH);// fermeture de la vanne

#if debug_serial                       
          Serial.print(" fait");
#endif
          } 
        }
        if (!in_action)
          {
            digitalWrite(pin_vanne,   HIGH);    // on ferme mes relais
            digitalWrite(pin_vanne+1, HIGH); 
            digitalWrite(pin_vanne+2, HIGH);
            digitalWrite(pin_vanne+3, HIGH);
            started_action=false;
          }
    }
    else
    {
      digitalWrite(pin_vanne,   HIGH);    // on ferme mes relais
      digitalWrite(pin_vanne+1, HIGH); 
      digitalWrite(pin_vanne+2, HIGH);
      digitalWrite(pin_vanne+3, HIGH);  
      started_action=false;
      }
// ####################################################################
// #              Gestion des deux boutons                            #
// ####################################################################

//################################################
//#             cyle forcé avec un appuye long   #
//################################################
if (digitalRead(pin_bypass)==0){
    bool flag=true;
    char count=0;
    while (count<10)  // teste que le bouton est maintenu 200ms
    {
      delay(20);
      count++;
      if (digitalRead(pin_bypass)!=0) {flag=false;}
    
    }
    if (flag) {set_bypass();}
}

//#####################################################################  
//#     réinitialisation de l'heure à 12h00 avec un appuie long       #
//#####################################################################
  if (digitalRead(pin_horloge)==0){
    bool flag=true;
    char count=0;
    while (count<10)  // teste que le bouton est maintenu 200ms
    {
      delay(20);
      count++;
      if (digitalRead(pin_horloge)!=0) {flag=false;}
    
    }
    if (flag) {
#if debug_serial
                Serial.print("\n réinitialisation de l'horloge");
#endif
                rtc.adjust(DateTime(2023, 1, 1, 12, 00, 0));   
                digitalWrite(pin_vanne,   HIGH);    // on ferme mes relais
                digitalWrite(pin_vanne+1, HIGH); 
                digitalWrite(pin_vanne+2, HIGH);
                digitalWrite(pin_vanne+3, HIGH);
                start_action=0;                   //on reinitialise toutes les actions
                started_action=false;
                bypass=false;
              }
  }

    
//##############################################################################
//#   boucle tant que le traitement du loop n'a pas duré au moins une seconde  # 
//##############################################################################
  bool boucle_temps=true;
  while (boucle_temps){
    ts2=millis();
    long d=(ts2>ts1)? ts2-ts1: (4294967295-ts1)+ts2;
    boucle_temps=(d<=1000);
  }
}