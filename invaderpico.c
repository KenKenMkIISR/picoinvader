//スペースインベーダー for PIC32MX250F128B / Color LCD Game System w/bootloader by K.Tanaka

#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "LCDdriver.h"
#include "graphlib.h"


// 入力ボタンのビット定義
#define GPIO_KEYUP 0
#define GPIO_KEYLEFT 1
#define GPIO_KEYRIGHT 2
#define GPIO_KEYDOWN 3
#define GPIO_KEYSTART 4
#define GPIO_KEYFIRE 5
#define KEYUP (1<<GPIO_KEYUP)
#define KEYLEFT (1<<GPIO_KEYLEFT)
#define KEYRIGHT (1<<GPIO_KEYRIGHT)
#define KEYDOWN (1<<GPIO_KEYDOWN)
#define KEYSTART (1<<GPIO_KEYSTART)
#define KEYFIRE (1<<GPIO_KEYFIRE)
#define KEYSMASK (KEYUP|KEYLEFT|KEYRIGHT|KEYDOWN|KEYSTART|KEYFIRE)

#define SOUNDPORT 6

#define clearscreen() LCD_Clear(0)

extern unsigned char bmp_missile1[],bmp_missile2[];

unsigned short keystatus,keystatus2,oldkey; //最新のボタン状態と前回のボタン状態
int ufox; //UFO X座標
int missilex,missiley; //自機ミサイル座標
int al_dir,al_x,al_y,al_conter; //インベーダー移動方向、左上座標、移動速度カウンター
int zanki; //自機残数
int explodecounter; //自機爆発中カウンタ
int ufo_dir,ufo_score,ufo_counter; //UFO移動方向、UFOスコア、UFO出現カウンタ
int stage; //ステージ番号
int gamestatus; //ゲームステータス
int al_animation; //インベーダーアニメーションカウンタ
int al_zan,cannonx,al_missilecount; //インベーダー残数、自機X座標、敵ミサイル出現カウンタ
int alien[5][11]; //インベーダー配列
int al_missilex1,al_missilex2,al_missiley1,al_missiley2; //敵ミサイルの座標（2つ）
unsigned int highscore,score; //ハイスコア、得点

//sound data
//上位16ビット 音の長さ（60分のn秒）　0の場合繰り返し数
//下位16ビット 音程 31250/f[Hz]*16
unsigned int SOUND1[]={0x000A018F,1};//FIRE
unsigned int SOUND2[]={0x00020354,0x000801AA,1};//AILEN EXPLOSION
unsigned int SOUND3[]={0x0003039B,0x00030371,1};//UFO MOVING
unsigned int SOUND4[]={0x00040429,0x00020000,3};//UFO SCORE
unsigned int SOUND5[]={0x00030FFF,0x00010000,30};//CANNON EXPLOSION
unsigned int * soundarray[]={SOUND1,SOUND2,SOUND3,SOUND4,SOUND5};

//Sound構造体
struct {
	unsigned int *p; //曲配列の演奏中の位置
	unsigned int *start; //曲配列の開始位置
	unsigned short count; //発音中の音カウンタ
	unsigned short loop; //曲配列繰り返しカウンタ
	unsigned char stop; //0:演奏中、1:終了
} Sound;

#define PWM_WRAP 4000 // 125MHz/31.25KHz
uint pwm_slice_num;

void sound_on(uint16_t f){
	pwm_set_clkdiv_int_frac(pwm_slice_num, f>>4, f&15);
	pwm_set_enabled(pwm_slice_num, true);
}
void sound_off(void){
	pwm_set_enabled(pwm_slice_num, false);
}
void playsound(void){
//60分の1秒ごとに呼び出し、効果音を変更
	if(Sound.stop) return; //サウンド停止中
	Sound.count--;
	if(Sound.count>0) return;
	Sound.p++; //次の音へ
	if(*Sound.p<0x10000){ //上位16ビットが0の場合、繰り返し
		Sound.loop--;
		if(Sound.loop==0){
			sound_off(); //サウンド停止
			Sound.stop=1;
			return;
		}
		Sound.p=Sound.start; //最初に戻る
	}
	Sound.count=*Sound.p>>16;
	sound_on(*Sound.p&0xffff); //音程設定
}
void sound(int n){
//効果音開始
	unsigned int *p;
	Sound.p=soundarray[n-1];
	Sound.start=Sound.p;
	Sound.count=*Sound.p>>16;
	sound_on(*Sound.p&0xffff); //音程設定
	Sound.stop=0;
	p=Sound.p;
	while(*p>=0x10000) *p++;
	Sound.loop=*p;
}
void wait60thsec(unsigned short n){
	// 60分のn秒ウェイト
	uint64_t t=to_us_since_boot(get_absolute_time())%16667;
	sleep_us(16667*n-t);
}
void wait60thsecwithsound(unsigned short n){
	// 60分のn秒ウェイト（効果音継続）
	while(n>0){
		wait60thsec(1); //60分の1秒ウェイト
		playsound();
		n--;
	}
}
void keycheck(void){
//ボタン状態読み取り
//keystatus :現在押されているボタンに対応するビットを1にする
//keystatus2:前回押されていなくて、今回押されたボタンに対応するビットを1にする
	oldkey=keystatus;
	keystatus=~gpio_get_all() & KEYSMASK;
	keystatus2=keystatus & ~oldkey; //ボタンから手を離したかチェック
}
void initgame(void){
//ゲーム初期化
	printstr(64,80,7,0,"SPACE ALIEN");
	printstr(48,110,7,0,"PUSH START BUTTON");
	while(1){
		//STARTキー待ち
		keycheck();
		if(keystatus & KEYSTART) break;
		wait60thsec(1);
		rand();
	}
	Sound.stop=1;
	stage=0;
	score=0;
	zanki=3;
	clearscreen();
}
void addscore(int s){
//得点追加
	score+=s;
	if(score>=99999) score=99999;
	if(score>highscore) highscore=score;
}
void printscore(void){
//得点表示
	printnum2(16,304,7,0,score,5);
	printnum2(136,304,7,0,highscore,5);
}
void putzanki(void){
//自機残数表示
	int i;
	printnum(8,208,5,0,zanki);
	for(i=1;i<zanki;i++){
		if(i>6) break;
		printstr(i*16+16,208,5,0,"\xa0\xa1");
	}
	for(;i<=6;i++){
		printstr(i*16+16,208,5,0,"  ");
	}
}
void clearchar(void){
// キャラクター表示消去
	//ミサイル消去
	if(missiley>0){
		boxfill(missilex,missiley,missilex+1,missiley+3,0);
	}
	else if(missiley==-1){
		boxfill(missilex-3,0,missilex+4,7,0);
	}
	if(al_missiley1>0){
		boxfill(al_missilex1,al_missiley1,al_missilex1+1,al_missiley1+3,0);
	}
	else if(al_missiley1==-1){
		boxfill(al_missilex1-2,198,al_missilex1+3,205,0);
		al_missiley1=0;
	}
	if(al_missiley2>0){
		boxfill(al_missilex2,al_missiley2,al_missilex2+1,al_missiley2+3,0);
	}
	else if(al_missiley2==-1){
		boxfill(al_missilex2-2,198,al_missilex2+3,205,0);
		al_missiley2=0;
	}
}
void clearalien(int x,int y){
//インベーダー表示消去（全体）
	int i,j,w;
	for(j=0;j<=4;j++){
		w=x;
		for(i=0;i<=10;i++){
			if(alien[j][i]) boxfill(w,y,w+15,y+7,0);
			w+=16;
		}
		y+=16;
	}
}
void fire(void){
//ミサイル発射チェック
	int p,q;
	if(explodecounter>0) return;
	if(missiley==0 && (keystatus2 & KEYFIRE)){
		//自機ミサイル発射
		rand();
		missiley=180;
		missilex=cannonx+8;
		sound(1);
	}
	if(al_missilecount>0) al_missilecount--; //敵ミサイルカウンタ
	if(al_missilecount==0 && (al_missiley1==0 || al_missiley2==0)){
		p=rand()%11;
		for(q=4;q>=0;q--){
			if(alien[q][p]>0) break;
		}
		if(q<0 || al_y+q*16>=176) return; //ミサイル発射できる高さにいない
		//敵ミサイル発射
		if(al_missiley1==0){
			al_missilex1=al_x+p*16+7;
			al_missiley1=al_y+q*16+8;
		}
		else{
			al_missilex2=al_x+p*16+7;
			al_missiley2=al_y+q*16+8;
		}
		al_missilecount=50;
	}
}
void movecannon(void){
//自機移動
	if(explodecounter>0){
		//自機爆発中
		explodecounter--;
		if(explodecounter==0){
			//爆発終了、残数表示更新、自機消去
			putzanki();
			boxfill(cannonx,184,cannonx+15,191,0);
			cannonx=8;
		}
		return;
	}
	if(cannonx>0   && (keystatus&(KEYLEFT|KEYRIGHT))==KEYLEFT ) cannonx--;
	if(cannonx<192 && (keystatus&(KEYLEFT|KEYRIGHT))==KEYRIGHT) cannonx++;
}
void movealien(void){
//インベーダー移動
	int a,i,s;
	if(explodecounter>0) return;//自機爆発中
	al_conter++;//敵移動カウンター
	//敵残数によって移動速度を変える
	if((al_zan>=20 && al_conter<20) || (al_zan>=12 && al_conter<10) ||
		(al_zan>=6 && al_conter<6) || (al_zan>=3 && al_conter<2) ||
		al_conter<1) return;
	al_conter=0;
	al_x+=al_dir;
	s=0;
	al_animation=1-al_animation;

	//左右移動できるところまで移動。端の場合1段下げる
	if(al_dir>0 && al_x>32){
		a=12-al_x/16;
		for(i=0;i<=4;i++){
			s+=alien[i][a];
		}
		if(s>0){
			al_x-=al_dir;
			al_y+=8;
			al_dir=-al_dir;
		}
	}
	else if(al_x<0){
		a=-al_x/16;
		for(i=0;i<=4;i++){
			s+=alien[i][a];
		}
		if(s>0){
			al_x-=al_dir;
			al_y+=8;
			al_dir=-al_dir;
		}
	}
	if(s>0) clearalien(al_x,al_y-8); //1段下がった場合、敵の表示全体を消去
}
void moveufo(void){
//UFO移動
	ufo_counter++;
	if(ufox>=0){
		//UFO出現中
		if(ufo_counter>0 && (ufo_counter&1)==0) ufox+=ufo_dir;
		if(ufo_counter==6){
			ufo_counter=0;
			if(explodecounter==0) sound(3);
		}
		if(ufox<0 || ufox>184 || ufo_counter==-1){
			//UFOが端から逃げた場合
			boxfill(ufox,8,ufox+23,15,0);
			ufox=-1;
			ufo_dir=-ufo_dir;
			ufo_counter=0;
		}
	}
	else{
		//UFOがいない時
		if(ufo_counter>=1500 && al_zan>7){
			ufo_counter=0;
			if(ufo_dir>0) ufox=0;
			else ufox=184;
			sound(3);
		}
	}
}
void movemissile(void){
//ミサイル移動
	if(missiley>0){
		missiley-=4;
		if(missiley<=0) missiley=-3;//てっぺんで爆発させるカウンタとする
	}
	else if(missiley<0) missiley++;//てっぺんで爆発中
	if(al_missiley1){
		al_missiley1++;
		if(al_missiley1>=200) al_missiley1=-3;//地面で爆発させるカウンタ
	}
	if(al_missiley2){
		al_missiley2++;
		if(al_missiley2>=200) al_missiley2=-3;//地面で爆発させるカウンタ
	}
}
void checkhit(void){
// ミサイルとインベーダーの衝突チェック
	int x,y;
	if(missilex< al_x    ) return;
	if(missilex>=al_x+176) return;
	if(missiley< al_y    ) return;
	if(missiley>=al_y+72 ) return;
	x=(missilex-al_x)/16;
	y=(missiley-al_y)/16;
	if((al_x+x*16+2 )>missilex) return;
	if((al_x+x*16+13)<missilex) return;
	if((al_y+y*16+2 )>missiley) return;
	if((al_y+y*16+15)<missiley) return;
	if(alien[y][x]<=0) return;
	if(explodecounter==0) sound(2);

	//インベーダーに命中
	addscore(alien[y][x]*10);
	alien[y][x]=-4; //爆発カウンター
	al_zan--;
	al_conter=-3; //爆発中で移動停止カウンター
	missiley=0;
}
void checkcollision(void){
//各種衝突チェック
	int s,i,j;
	// インベーダーとミサイル
	if(missiley>0) checkhit();

	// UFOとミサイル
	if(missiley>=8 && missiley<16 && ufox>=0 && ufo_counter>=0){
		if(missilex>=ufox+4 && missilex<ufox+19){
			missiley=0;
			sound(2);
			ufo_counter=-25;//UFO爆発カウンター
			ufo_score=((rand()&3)+2)*50;
			if(ufo_score==250) ufo_score=300;
			addscore(ufo_score);
		}
	}

	// ミサイル同士の衝突チェック
	if(missiley>0){
		if(missilex==al_missilex1 && missiley>=al_missiley1 && missiley<al_missiley1+4){
			missiley=0;
			al_missiley1=0;
			sound(2);
		}
		if(missilex==al_missilex2 && missiley>=al_missiley2 && missiley<al_missiley2+4){
			missiley=0;
			al_missiley2=0;
			sound(2);
		}
	}

	// 自機と敵ミサイルチェック
	if(explodecounter==0){
		if(al_missiley1>181 && al_missiley1<192 && al_missilex1>=cannonx+2 && al_missilex1<=cannonx+14 ||
		  (al_missiley2>181 && al_missiley2<192 && al_missilex2>=cannonx+2 && al_missilex2<=cannonx+14)){
			explodecounter=120;
			sound(5);
		}
	}

	// ミサイルとトーチカのチェック
	if(missiley>160){
		s=getColor(missilex,missiley)|getColor(missilex,missiley+1)|
			getColor(missilex,missiley+2)|getColor(missilex,missiley+3);
		if(s){
			boxfill(missilex-1,missiley,missilex+1,missiley+3,0);
			missiley=0;
		}
	}
	if(al_missiley1>160){
		s=getColor(al_missilex1,al_missiley1)|getColor(al_missilex1,al_missiley1+1)|
			getColor(al_missilex1,al_missiley1+2)|getColor(al_missilex1,al_missiley1+3);
		if(s){
			boxfill(al_missilex1-1,al_missiley1-1,al_missilex1+2,al_missiley1+5,0);
			al_missiley1=0;
		}
	}
	if(al_missiley2>160){
		s=getColor(al_missilex2,al_missiley2)|getColor(al_missilex2,al_missiley2+1)|
			getColor(al_missilex2,al_missiley2+2)|getColor(al_missilex2,al_missiley2+3);
		if(s){
			boxfill(al_missilex2-1,al_missiley2-1,al_missilex2+2,al_missiley2+5,0);
			al_missiley2=0;
		}
	}
}
void putmissile(void){
//ミサイル表示
	if(missiley>0) putbmpmn(missilex,missiley,1,4,bmp_missile1);
	else if(missiley<=-2) putfont(missilex-3,0,2,0,0x90); //てっぺんで爆発中
	if(al_missiley1>0) putbmpmn(al_missilex1,al_missiley1,2,4,bmp_missile2);
	if(al_missiley2>0) putbmpmn(al_missilex2,al_missiley2,2,4,bmp_missile2);
	if(al_missiley1<0) putfont(al_missilex1-2,198,2,0,0x91); //地面で爆発中
	if(al_missiley2<0) putfont(al_missilex2-2,198,2,0,0x91); //地面で爆発中
}
void putalien1(int x,int y,int n){
//インベーダーを1個表示
	int c,p;
	if(n==-1){
		//1個分消去
		boxfill(x,y,x+15,y+7,0);
		return;
	}
	if(al_conter>0) return;
	if(n<0) p=0x8c; //爆発中
	else p=0x80+(n-1)*4+al_animation*2;

	//行によって色を変える
	if(y<8) c=2;
	else if(y<32) c=3;
	else if(y<64) c=4;
	else if(y<96) c=5;
	else if(y<128) c=3;
	else if(y<160) c=6;
	else c=2;
	putfont(x,y,c,0,p);
	putfont(x+8,y,c,0,p+1);
}
void putaliens(void){
//インベーダーの全体表示
	int x,y,i,j,w,n;
	x=al_x;
	y=al_y;
	for(j=0;j<=4;j++){
		w=x;
		for(i=0;i<=10;i++){
			n=alien[j][i];
			if(n) putalien1(w,y,n);
			if(n<0) alien[j][i]++; //爆発中カウンター
			w+=16;
		}
		y+=16;
	}
}
void putufo(void){
//UFO表示
	if(ufox<0) return;
	if(ufo_counter>=0){
		printstr(ufox,8,3,0,"\xb0\xb1\xb2");
	}
	else if(ufo_counter==-24){
		//UFO爆発中
		printstr(ufox,8,3,0,"\xb3\xb4\xb5");
		sound(2);
	}
	else if(ufo_counter==-18){
		//UFO得点表示中
		printnum(ufox,8,3,0,ufo_score);
		sound(4);
	}
}
void putcannon(void){
//自機表示
	if(explodecounter==0){
		printstr(cannonx,184,5,0,"\xa0\xa1");
	}
	else{
		//爆発中
		if(explodecounter & 2) printstr(cannonx,184,5,0,"\xa2\xa3");
		else printstr(cannonx,184,5,0,"\xa4\xa5");
	}
}
int checkgame(void){
//ゲームステータスを更新
	int a,i,s;
	if(explodecounter==120){
		//自機がやられた直後
		zanki--;
		if(zanki==0) return 2;//ゲームオーバー
	}
	if(al_zan==0) return 1; //敵全滅、次ステージへ
	if(al_y>=120){
		s=0;
		a=11-(al_y-8)/16;
		for(i=0;i<=10;i++) s+=alien[a][i];
		if(s>0) return 2;//インベーダーが地面まで侵略してゲームオーバー
	}
	return 0;
}
void gameover(void){
//ゲームオーバー処理
	putzanki();
	printstr(74,130,4,0,"GAME OVER");
	wait60thsecwithsound(240);
}
void nextstage(void){
//次ステージへ進む処理
	int i,x;
	stage++;
	if(stage>=2){ //ステージ1の場合は飛ばす
		printstr(40,60,6,0,"CONGRATULATIONS!");
		wait60thsecwithsound(60);
		printstr(60,85,6,0,"NEXT STAGE");
		wait60thsec(120);
	}
	clearscreen();
	printstr(16,296,5,0,"SCORE");
	printstr(128,296,2,0,"HI-SCORE");
	printstr(144,208,4,0,"STAGE");
	printnum(192,208,5,0,stage);
	printscore();
	putzanki();

	//インベーダー初期化
	for(i=0;i<=10;i++) alien[0][i]=3;
	for(i=0;i<=10;i++) alien[1][i]=2;
	for(i=0;i<=10;i++) alien[2][i]=2;
	for(i=0;i<=10;i++) alien[3][i]=1;
	for(i=0;i<=10;i++) alien[4][i]=1;

	boxfill(0,206,215,207,2);//地面表示

	//トーチカ表示
	x=21;
	for(i=1;i<=4;i++){
		printstr(x,160,2,0,"\xf0\xf1\xf2");
		printstr(x,168,2,0,"\xf3\xf4\xf5");
		x+=48;
	}

	printstr(80,100,4,0,"STAGE ");
	printnum(128,100,4,0,stage);
	wait60thsec(180);
	boxfill(60,100,150,107,0);

	//各種パラーメータ設定
	al_x=16;
	al_y=((stage-1)%8)*8+32; //ステージによってインベーダー高さ設定
	al_dir=2;
	cannonx=8;
	ufox=-1;
	ufo_counter=0;
	ufo_dir=1;
	explodecounter=0;
	missiley=0;
	al_animation=0;
	al_conter=0;
	al_zan=55;
	al_missiley1=0;
	al_missiley2=0;
	al_missilecount=50;
	keycheck();
}

void main(void){
    stdio_init_all();

	// ボタン用GPIO設定
	gpio_init_mask(KEYSMASK);
	gpio_set_dir_in_masked(KEYSMASK);
	gpio_pull_up(GPIO_KEYUP);
	gpio_pull_up(GPIO_KEYLEFT);
	gpio_pull_up(GPIO_KEYRIGHT);
	gpio_pull_up(GPIO_KEYDOWN);
	gpio_pull_up(GPIO_KEYSTART);
	gpio_pull_up(GPIO_KEYFIRE);

	// サウンド用PWM設定
	gpio_set_function(SOUNDPORT, GPIO_FUNC_PWM);
	pwm_slice_num = pwm_gpio_to_slice_num(SOUNDPORT);
	pwm_set_wrap(pwm_slice_num, PWM_WRAP-1);
	// duty 50%
	pwm_set_chan_level(pwm_slice_num, PWM_CHAN_A, PWM_WRAP/2);

	// 液晶用ポート設定
    // Enable SPI 0 at 40 MHz and connect to GPIOs
    spi_init(SPICH, 40000 * 1000);
    gpio_set_function(PICO_DEFAULT_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);

	gpio_init(LCD_CS);
	gpio_put(LCD_CS, 1);
	gpio_set_dir(LCD_CS, GPIO_OUT);
	gpio_init(LCD_DC);
	gpio_put(LCD_DC, 1);
	gpio_set_dir(LCD_DC, GPIO_OUT);
	gpio_init(LCD_RESET);
	gpio_put(LCD_RESET, 1);
	gpio_set_dir(LCD_RESET, GPIO_OUT);

	init_graphic(); //液晶利用開始
	LCD_WriteComm(0x37); //画面中央にするためスクロール設定
	LCD_WriteData2(272-24);
	highscore=0;
	while(1){
		initgame(); //ゲーム初期化
		do{
			nextstage(); //次ステージへ
			do{
				wait60thsecwithsound(1); //60分の1秒ウェイトと効果音更新
				clearchar(); //キャラクター表示消去
				keycheck(); //ボタン押下状態読み込み
				fire(); //ミサイル発射処理
				movecannon(); //自機移動
				movealien(); //インベーダー移動
				moveufo(); //UFO移動
				movemissile(); //ミサイル移動
				checkcollision(); //各種衝突チェック
				putmissile(); //ミサイル表示
				putaliens(); //インベーダー表示
				putufo(); //UFO表示
				putcannon(); //自機表示
				printscore(); //得点表示
				gamestatus=checkgame(); //ゲームステータス更新
			} while(gamestatus==0);
			//gamestatus 0:通常、1:次ステージへ、2:ゲームオーバー
		} while(gamestatus==1);
		gameover(); //ゲームオーバー
	}
}
