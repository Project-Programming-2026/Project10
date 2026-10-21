
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace std;

static const string output_path = "trueskill.csv";
static const string input_path_players = "players.csv";
static const string input_path_matches = "matches.csv";

static constexpr double initial_mu = 25.0;
static constexpr double initial_sigma = 25.0 / 3.0;
static constexpr double performance_beta = 25.0 / 6.0;
static constexpr double tau = 25.0 / 300.0;
static constexpr int evaluation_interval_matches_per_player = 10;

class Player {
    int id;
    double mu = initial_mu;
    double sigma = initial_sigma;
    int matches_played = 0;

public:
    explicit Player(int player_id) : id(player_id) {}

    double get_mu() const { return mu; }
    double get_sigma() const { return sigma; }
    int get_matches_played() const { return matches_played; }

    void set_rating(double new_mu, double new_sigma) {
        if (!isfinite(new_mu) || !isfinite(new_sigma) || new_sigma <= 0.0) {
            throw runtime_error("このプレイヤーのスキル、なんか変...? → " + to_string(id));
        }
        mu = new_mu;
        sigma = new_sigma;
        ++matches_played;
    }
};

static double normal_pdf(double x) {
    static const double inv_sqrt_2pi = 1.0 / sqrt(2.0 * acos(-1.0));
    return inv_sqrt_2pi * exp(-0.5 * x * x);
}

static double normal_cdf(double x) {
    return 0.5 * erfc(-x / sqrt(2.0));
}

static double inverse_mills_ratio(double x) {
    if (x >= -10.0) {
        return normal_pdf(x) / normal_cdf(x);
    }
    const double y = -x;
    double fraction = 0.0;
    for (int k = 80; k >= 1; --k) {
        fraction = static_cast<double>(k) / (y + fraction);
    }
    return y + fraction;
}

static void update_match(Player& a, Player& b, double result_a) {
    if (&a == &b) {
        throw invalid_argument("自分と戦ってますよ！早く気付いて！");
    }
    if (result_a != 0.0 && result_a != 1.0) {
        throw invalid_argument("引き分けとかは現在実装されてません。");
    }

    const double mu_a = a.get_mu();
    const double mu_b = b.get_mu();
    const double q_a = a.get_sigma() * a.get_sigma() + tau * tau;
    const double q_b = b.get_sigma() * b.get_sigma() + tau * tau;
    const double c2 = q_a + q_b + 2.0 * performance_beta * performance_beta;
    const double c = sqrt(c2);
    const double direction = result_a == 1.0 ? 1.0 : -1.0;
    const double t = direction * (mu_a - mu_b) / c;
    const double v = inverse_mills_ratio(t);
    const double w = fmin(1.0, fmax(0.0, v * (v + t)));

    const double new_mu_a = mu_a + direction * q_a / c * v;
    const double new_mu_b = mu_b - direction * q_b / c * v;
    const double new_sigma_a = sqrt(q_a * (1.0 - q_a / c2 * w));
    const double new_sigma_b = sqrt(q_b * (1.0 - q_b / c2 * w));

    a.set_rating(new_mu_a, new_sigma_a);
    b.set_rating(new_mu_b, new_sigma_b);
}

static double predict(const Player& a, const Player& b) {
    const double variance = a.get_sigma() * a.get_sigma()
                          + b.get_sigma() * b.get_sigma()
                          + 2.0 * performance_beta * performance_beta;
    return normal_cdf((a.get_mu() - b.get_mu()) / sqrt(variance));
}

static void output(ofstream& out, int run_id, const map<int, Player>& players,
                   int matches_per_player) {
    for (auto it_a = players.begin(); it_a != players.end(); ++it_a) {
        for (auto it_b = next(it_a); it_b != players.end(); ++it_b) {
            out << run_id << ',' << matches_per_player << ','
                << it_a->first << ',' << it_b->first << ','
                << predict(it_a->second, it_b->second) << '\n';
        }
    }
    if (!out) throw runtime_error("trueskill.csvがよくわからんけど書き込めませんでした。");
}

void generate_trueskill_prediction(int run_id) {
    ifstream in_players(input_path_players);
    if (!in_players) throw runtime_error("players.csvが見つかりませんでした。パス見直そうね。");

    map<int, Player> players;
    string line;
    getline(in_players, line);
    while (getline(in_players, line)) {
        if (line.empty() || line == "\r") continue;
        size_t pos = 0;
        const int id = stoi(line, &pos);
        if (line.substr(pos).find_first_not_of(" \t\r") != string::npos || id < 0) {
            throw runtime_error("プレイヤーIDが変なので登録できませんでした" + line);
        }
        if (!players.emplace(id, Player(id)).second) {
            throw runtime_error("このプレイヤーID、もうありますね → " + to_string(id));
        }
    }
    if (players.size() < 2) throw runtime_error("流石に0プレイヤー、1プレイヤーの戦闘はないでしょう...");

    ifstream in_matches(input_path_matches);
    if (!in_matches) throw runtime_error("matches.csv...なくね？");

    ofstream out(output_path);
    if (!out) throw runtime_error("trueskill.csvが何か作れませんでした。なんでだろうね。");
    out << "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins\n";
    out << setprecision(17);

    const long long checkpoint_matches =
        static_cast<long long>(players.size()) * evaluation_interval_matches_per_player / 2;
    if (checkpoint_matches * 2 !=
        static_cast<long long>(players.size()) * evaluation_interval_matches_per_player) {
        throw runtime_error("インターバルが変です");
    }

    long long processed_matches = 0;
    int last_period_id = -1;
    long long last_match_id = -1;
    getline(in_matches, line);
    while (getline(in_matches, line)) {
        if (line.empty() || line == "\r") continue;

        stringstream ss(line);
        string value;
        vector<string> row;
        while (getline(ss, value, ',')) row.push_back(value);
        if (row.size() != 5) throw runtime_error("ここで形が変になってます。" + line);

        const long long match_id = stoll(row.at(0));
        const int period_id = stoi(row.at(1));
        const int id_a = stoi(row.at(2));
        const int id_b = stoi(row.at(3));
        const double result_a = stod(row.at(4));
        if (match_id <= last_match_id || period_id < last_period_id) {
            throw runtime_error("matches.csvのソートがなんか変です。");
        }
        if (id_a == id_b) throw runtime_error("同じプレイヤーになってます。");
        if (result_a != 0.0 && result_a != 1.0) {
            throw runtime_error("勝ちか負けかの戦いしか認められていません。");
        }

        update_match(players.at(id_a), players.at(id_b), result_a);
        ++processed_matches;
        last_match_id = match_id;
        last_period_id = period_id;

        if (processed_matches % checkpoint_matches == 0) {
            const long long expected_matches = processed_matches * 2 / players.size();
            for (const auto& [id, player] : players) {
                if (player.get_matches_played() != expected_matches) {
                }
            }
            output(out, run_id, players, static_cast<int>(expected_matches));
        }
    }
}
