#include <cmath>
#include <vector>
#include <map>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
using namespace std;

static string output_pass = "glicko2.csv"; //一旦カレントディレクトリとする
static string input_pass_players = "players.csv";
static string input_pass_matches = "matches.csv";
static const double tau = 0.5; //ボラティリティの変動しやすさ
static const double epsilon = 0.000001; //小さい定数
static const double pi = 3.141592653589793; //円周率

class Player;

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
    void update(); //レートの更新
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

void Player::update(){
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
    for(int i=0;i<past_result.size();i++){
        opponents_mu.at(i) = scale_rate(opponents_r.at(i));
        opponents_phi.at(i) = scale_rd(opponents_rd.at(i));
        Evec.at(i) = E(mu,opponents_mu.at(i),opponents_phi.at(i));
    }
    //vの計算
    double vinv = 0;
    for(int i=0;i<past_result.size();i++){
        vinv += pow(g(opponents_phi.at(i)),2) * Evec.at(i) * (1 - Evec.at(i));
    }
    if(!std::isfinite(vinv) || vinv <= 0){ //極端なvinvによるデータの破壊検知
        cerr << "invalid vinv: player " << id << endl;
        throw runtime_error("Glicko-2 numerical error");
    }
    double v = 1 / vinv;
    //deltaの計算
    double delta = 0;
    for(int i=0;i<past_result.size();i++){
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
    mu = mu + pow(phi,2)*delta/v;
    r = rescale_rate(mu);
    rd = rescale_rd(phi);
    //履歴のリフレッシュ
    past_result.clear();
    opponents_r.clear();
    opponents_rd.clear();
    opponents_sigma.clear();
    attend = false;
}

//全プレイヤーの更新(レート期間の終了)
void update_all(){
    for(auto [id, player]: players) player->update();
}

double predict(int i, int j){
    double ri = players.at(i)->get_rate();
    double rj = players.at(j)->get_rate();
    double rdi = players.at(i)->get_rd();
    double rdj = players.at(j)->get_rd();
    return 1 / (1 + pow(10,-g1(sqrt(pow(rdi,2)+pow(rdj,2)))*(ri-rj)/400));
}

void output(int run_id, int period){
    ofstream out(output_pass,ios::app);
    sort(exist_player.begin(),exist_player.end());
    for(int i=0; i<exist_player.size(); i++) for(int j=i+1; j<exist_player.size();j++){
        int a = exist_player.at(i);
        int b = exist_player.at(j);
        out << run_id << "," << period*10 + 10 << "," << a << "," << b << "," << predict(a,b) << endl;
    }
    out.close();
}

void generate_glicko2_prediction(int run_id){
    //glicko2.csvの作成と1行目の記述
    ofstream out(output_pass);
    if(!out){
        cerr << "could not make glicko2.csv." << endl;
        return;
    }
    out << "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins" << endl;
    out.close();
    string line;
    //プレイヤー登録
    ifstream in_players(input_pass_players);
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
    //対戦
    int current_period = 0;
    ifstream in_matches(input_pass_matches);
    if(!in_matches){
        cerr << "could not open matches.csv." << endl;
        return;
    }
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
            update_all();
            output(run_id,current_period);
            current_period = stoi(row.at(1));
        }
        //対戦結果の登録
        players.at(stoi(row.at(2)))->push_result(stod(row.at(4)),stoi(row.at(3)));
        players.at(stoi(row.at(3)))->push_result(1 - stod(row.at(4)),stoi(row.at(2)));
    }
    in_matches.close();
    //最後に一回更新、集計
    update_all();
    output(run_id,current_period);

    for(auto [_, player]:players) delete(player);
}

/*
int main(){
    generate_glicko2_prediction(-1);
}
*/