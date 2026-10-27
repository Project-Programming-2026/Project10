#include "project10/ratings/glicko2.hpp"

#include <cmath>
#include <vector>
#include <map>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
using namespace std;
namespace fs = filesystem;

namespace{

static fs::path output_file = "ranked_glicko2.csv";
static fs::path input_file_rating = "glicko2_rating.csv";
static fs::path input_file_players = "players.csv";
static fs::path input_file_matches = "matches.csv";
static const double tau = 0.5; //ボラティリティの変動しやすさ
static const double alpha = 0.25; //分布補正の最大補正幅
static const double c = 0.01; //分布補正の感度
static const double epsilon = 0.000001; //小さい定数
static const double pi = atan(1) * 4;

class Player;
class Distribution;

static vector<int> exist_player; //存在するプレイヤーのIDを記憶するvector
static map<int,Player*> players; //プレイヤーの情報をIDキーで格納するmap

double scale_rate(double rate){
    return (rate - 1500) / 173.7178;
}

double rescale_rate(double mu){
    return 173.7178 * mu + 1500;
}

double scale_rd(double rd){
    return rd / 173.7178;
}

double rescale_rd(double phi){
    return 173.7178 * phi;
}

double g(double phi){
    return 1 / sqrt(1 + (3 * pow(phi,2) / pow(pi,2)));
}

double g1(double rd){
    return 1 / sqrt(1 + (3 * pow(0.0057565,2) * pow(rd,2))/pow(pi,2));
}

double E(double mu,double muj,double phij){
    return 1 / (1 + exp(-g(phij) * (mu - muj)));
}

double D(Distribution ideal, Distribution current, double rate);

class Player{
    int id; // プレイヤーID
    double r; // レート
    double rd; // RD(不確かさ)
    double sigma; // ボラティリティ(揮発度)
    vector<double> past_result; //対戦結果 Glicko-2は引き分けなどにも対応できるのでdoubleで定義
    vector<double> opponents_r; //対戦相手のレート
    vector<double> opponents_rd; //対戦相手のRD
    vector<double> opponents_sigma; //対戦相手のボラティリティ
    bool attend; //レート期間中に対戦したかどうか
public:
    Player(int _id):id(_id),r(1500),rd(350),sigma(0.06),attend(false){
        past_result.clear();
        opponents_r.clear();
        opponents_rd.clear();
        opponents_sigma.clear();
    }
    double get_rate(); //レートの取得
    double get_rd(); //RDの取得
    void push_result(double result, int opponent); //試合結果の保存
    void update(Distribution ideal, Distribution current); //レートの更新
};

class Distribution{
    bool initialized; //初期操作をしたかどうか
    map<int,double> distribution; //分布
public:
    Distribution():initialized(false){
        distribution.emplace(-6,0); //~950
        distribution.emplace(-5,0); //950~1050
        distribution.emplace(-4,0); //1050~1150 
        distribution.emplace(-3,0); //1150~1250
        distribution.emplace(-2,0); //1250~1350
        distribution.emplace(-1,0); //1350~1450
        distribution.emplace(0,0); //1450~1550
        distribution.emplace(1,0); //1550~1650
        distribution.emplace(2,0); //1650~1750
        distribution.emplace(3,0); //1750~1850
        distribution.emplace(4,0); //1850~1950
        distribution.emplace(5,0); //1950~2050
        distribution.emplace(6,0); //2050~
    }
    bool get_initialized();
    double get_rank_ratio(int rank);
    void initialize_ideal(fs::path file_path); //理想分布の初期操作
    void initialize_current(); //現在分布の初期操作
    void reset(); //初期操作前に戻す
};

double Player::get_rate(){return r;}
double Player::get_rd(){return rd;}

void Player::push_result(double result, int opponent){
    past_result.push_back(result);
    opponents_r.push_back(players.at(opponent)->r);
    opponents_rd.push_back(players.at(opponent)->rd);
    opponents_sigma.push_back(players.at(opponent)->sigma);
    attend = true;
}

void Player::update(Distribution ideal, Distribution current){
    //期間中に対戦していないなら、特殊な処理
    if(!attend){
        rd = rescale_rd(sqrt(pow(scale_rd(rd),2) + pow(sigma,2)));
        return;
    }
    //スケーリング
    double mu = scale_rate(r);
    double phi = scale_rd(rd);
    vector<double> opponents_mu(past_result.size());
    vector<double> opponents_phi(past_result.size());
    vector<double> Evec(past_result.size());
    for(int i=0;i<(int)past_result.size();i++){
        opponents_mu.at(i) = scale_rate(opponents_r.at(i));
        opponents_phi.at(i) = scale_rd(opponents_rd.at(i));
        Evec.at(i) = E(mu,opponents_mu.at(i),opponents_phi.at(i));
    }
    //vの計算
    double vinv = 0;
    for(int i=0;i<(int)past_result.size();i++){
        vinv += pow(g(opponents_phi.at(i)),2) * Evec.at(i) * (1 - Evec.at(i));
    }
    if(!std::isfinite(vinv) || vinv <= 0){ //極端なvinvによるデータの破壊検知
        cerr << "invalid vinv: player " << id << endl;
        throw runtime_error("Glicko-2 numerical error");
    }
    double v = 1 / vinv;
    //deltaの計算
    double delta = 0;
    for(int i=0;i<(int)past_result.size();i++){
        delta += v * g(opponents_phi.at(i)) * (past_result.at(i) - Evec.at(i));
    }
    //新しいsigmaの計算
    double a0 = log(pow(sigma,2));
    auto f = [&](double x){
        return exp(x)*(pow(delta,2) - pow(scale_rd(rd),2) - v - exp(x))/(2*pow((pow(scale_rd(rd),2) + v + exp(x)),2)) - (x-a0)/pow(tau,2);
    };
    double a = a0;
    double b;
    if(pow(delta,2) > pow(phi,2)+v) b = log(pow(delta,2) - pow(phi,2) - v);
    else{
        int k = 1;
        while(f(a - k*tau) < 0) k++;
        b = a - k*tau;
    }
    double fa = f(a);
    double fb = f(b);
    while(abs(b-a) > epsilon){
        double c = a + (a - b)*fa/(fb - fa);
        double fc = f(c);
        if(fc*fb <= 0){
            a = b; fa = fb;
        }else fa = fa/2;
        b = c; fb = fc;
    }
    sigma = exp(a/2);
    //レート、RDの更新
    double phis = sqrt(pow(phi,2) + pow(sigma,2));
    phi = 1 / sqrt(pow(phis,-2) + vinv);
    mu = mu + D(ideal,current,r)*pow(phi,2)*delta/v;
    r = rescale_rate(mu);
    rd = rescale_rd(phi);
    //履歴のリフレッシュ
    past_result.clear();
    opponents_r.clear();
    opponents_rd.clear();
    opponents_sigma.clear();
    attend = false;
}

bool Distribution::get_initialized(){return initialized;}
double Distribution::get_rank_ratio(int rank){return distribution.at(rank);}

void Distribution::initialize_ideal(fs::path file_path){
    if(initialized){
        cerr << "you cannot initialize Distribution two times." << endl;
        throw runtime_error("The source code has fatal issue on Distribution::initialize_ideal.");
    }
    ifstream in_rating(file_path / input_file_rating);
    if(!in_rating){
        cerr << "could not open glicko2_rating.csv." << endl;
        throw runtime_error("Failed to open: glicko2_rating.csv");
    }
    string line;
    getline(in_rating,line);
    double sum = 0.0;
    while(getline(in_rating,line)){
        sum += 1.0;
        double rate = stod(line);
        if(rate < 950) distribution.at(-6) += 1.0;
        else if(rate < 1050) distribution.at(-5) += 1.0;
        else if(rate < 1150) distribution.at(-4) += 1.0;
        else if(rate < 1250) distribution.at(-3) += 1.0;
        else if(rate < 1350) distribution.at(-2) += 1.0;
        else if(rate < 1450) distribution.at(-1) += 1.0;
        else if(rate < 1550) distribution.at(0) += 1.0;
        else if(rate < 1650) distribution.at(1) += 1.0;
        else if(rate < 1750) distribution.at(2) += 1.0;
        else if(rate < 1850) distribution.at(3) += 1.0;
        else if(rate < 1950) distribution.at(4) += 1.0;
        else if(rate < 2050) distribution.at(5) += 1.0;
        else distribution.at(6) += 1.0;
    }
    in_rating.close();
    for(auto [counter, _]:distribution) distribution.at(counter) /= sum;
    initialized = true;
}

void Distribution::initialize_current(){
    if(initialized){
        cerr << "you cannot initialize Distribution two times." << endl;
        throw runtime_error("The source code has issue on Distribution::initialize_current.");
    }
    double sum = 0.0;
    for(auto [_, player]:players){
        sum += 1.0;
        double rate = player->get_rate();
        if(rate < 950) distribution.at(-6) += 1.0;
        else if(rate < 1050) distribution.at(-5) += 1.0;
        else if(rate < 1150) distribution.at(-4) += 1.0;
        else if(rate < 1250) distribution.at(-3) += 1.0;
        else if(rate < 1350) distribution.at(-2) += 1.0;
        else if(rate < 1450) distribution.at(-1) += 1.0;
        else if(rate < 1550) distribution.at(0) += 1.0;
        else if(rate < 1650) distribution.at(1) += 1.0;
        else if(rate < 1750) distribution.at(2) += 1.0;
        else if(rate < 1850) distribution.at(3) += 1.0;
        else if(rate < 1950) distribution.at(4) += 1.0;
        else if(rate < 2050) distribution.at(5) += 1.0;
        else distribution.at(6) += 1.0;
    }
    for(auto [counter, _]:distribution) distribution.at(counter) /= sum;
    initialized = true;
}

void Distribution::reset(){
    for(auto [counter, _]:distribution) distribution.at(counter) = 0.0;
    initialized = false;
}

double D(Distribution ideal, Distribution current, double rate){
    if(!ideal.get_initialized() || !current.get_initialized()){
        cerr << "you cannot use uninitialized Distribution." << endl;
        throw runtime_error("The source code has issue on D.");
    }
    int rank;
    if(rate < 950) rank = -6;
    else if(rate < 1050) rank = -5;
    else if(rate < 1150) rank = -4;
    else if(rate < 1250) rank = -3;
    else if(rate < 1350) rank = -2;
    else if(rate < 1450) rank = -1;
    else if(rate < 1550) rank = 0;
    else if(rate < 1650) rank = 1;
    else if(rate < 1750) rank = 2;
    else if(rate < 1850) rank = 3;
    else if(rate < 1950) rank = 4;
    else if(rate < 2050) rank = 5;
    else rank = 6;
    double e = pow(ideal.get_rank_ratio(rank) - current.get_rank_ratio(rank), 2);
    return 1 + (alpha * (e / (e + pow(c,2)))); 
}

//全プレイヤーの更新(レート期間の終了)
void update_all(Distribution ideal, Distribution &current){
    for(auto [id, player]: players) player->update(ideal,current);
    current.reset();
    current.initialize_current();
}

double predict(int i, int j){
    double ri = players.at(i)->get_rate();
    double rj = players.at(j)->get_rate();
    double rdi = players.at(i)->get_rd();
    double rdj = players.at(j)->get_rd();
    return 1 / (1 + pow(10,-g1(sqrt(pow(rdi,2)+pow(rdj,2)))*(ri-rj)/400));
}

void output(int run_id, int period, fs::path file_path){
    ofstream out(file_path / output_file,ios::app);
    sort(exist_player.begin(),exist_player.end());
    for(int i=0; i<(int)exist_player.size(); i++) for(int j=i+1; j<(int)exist_player.size();j++){
        int a = exist_player.at(i);
        int b = exist_player.at(j);
        out << run_id << "," << period*10 + 10 << "," << a << "," << b << "," << predict(a,b) << endl;
    }
    out.close();
}

} //namespace

void generate_ranked_glicko2_prediction(int run_id, fs::path file_path){
    string line;
    //プレイヤー登録
    ifstream in_players(file_path / input_file_players);
    if(!in_players){
        cerr << "could not open players.csv." << endl;
        return;
    }
    getline(in_players,line); //一回読み飛ばす
    while(getline(in_players,line)){
        int id = stoi(line);
        exist_player.push_back(id);
        players.emplace(id,new Player(id));
    }
    in_players.close();
    //理想分布、現在分布の作成
    Distribution ideal, current;
    ideal.initialize_ideal(file_path);
    current.initialize_current();
    //対戦
    int current_period = 0;
    ifstream in_matches(file_path / input_file_matches);
    if(!in_matches){
        cerr << "could not open matches.csv." << endl;
        return;
    }
    //glicko2.csvの作成と1行目の記述
    ofstream out(file_path / output_file);
    if(!out){
        cerr << "could not make glicko2.csv." << endl;
        return;
    }
    out << "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins" << endl;
    out.close();
    getline(in_matches,line); //一回読み飛ばす
    while(getline(in_matches,line)){
        stringstream ss(line);
        string value;
        vector<string> row;
        while(getline(ss,value,',')) row.push_back(value);
        //コメント行の処理をする場合ここに追加
        if(row.size() != 5){
            cerr << "invalid data in matches.csv." << endl;
            return;
        }
        //period_idが変化したら、更新、集計
        if(stoi(row.at(1)) != current_period){
            update_all(ideal,current);
            output(run_id,current_period,file_path);
            current_period = stoi(row.at(1));
        }
        //対戦結果の登録
        if(row.at(2) == row.at(3)){
            cerr << "self matching refused." << endl;
            continue;
        }
        players.at(stoi(row.at(2)))->push_result(stod(row.at(4)),stoi(row.at(3)));
        players.at(stoi(row.at(3)))->push_result(1 - stod(row.at(4)),stoi(row.at(2)));
    }
    in_matches.close();
    //最後に一回更新、集計
    update_all(ideal,current);
    output(run_id,current_period,file_path);
    
    for(auto [_, player]:players) delete(player);
}

