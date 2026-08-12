#define _USE_MATH_DEFINES
#include <crow.h>
#include <sqlite3.h>
#include "embedded_assets.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iostream>
#include <fstream>
#include <functional>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// ============================================================
// AYKO Operations Platform - Backend C++
// Hackathon Inovahack 2026 - Equipe Kaino
// ============================================================

using namespace std;

// ==================== DATABASE MANAGER ====================

class Database {
private:
    sqlite3* db;
    mutex db_mutex;

public:
    Database(const string& path) {
        int rc = sqlite3_open(path.c_str(), &db);
        if (rc) {
            cerr << "Cannot open database: " << sqlite3_errmsg(db) << endl;
            return;
        }
        initialize_tables();
        seed_data();
        seed_traffic();
        seed_gestor_and_pops();
        seed_priority_data();
    }

    ~Database() {
        sqlite3_close(db);
    }

    void initialize_tables() {
        const char* sql = R"(
            CREATE TABLE IF NOT EXISTS users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                username TEXT UNIQUE NOT NULL,
                password TEXT NOT NULL,
                role TEXT NOT NULL,
                name TEXT NOT NULL,
                email TEXT,
                phone TEXT,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS technicians (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                user_id INTEGER REFERENCES users(id),
                skill_level INTEGER DEFAULT 1,
                specialties TEXT,
                current_status TEXT DEFAULT 'IDLE',
                latitude REAL DEFAULT -20.3155,
                longitude REAL DEFAULT -40.3128,
                current_task_id INTEGER,
                completed_tasks INTEGER DEFAULT 0,
                avg_resolution_time REAL DEFAULT 0,
                rating REAL DEFAULT 5.0,
                last_update DATETIME DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS tickets (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                client_id INTEGER REFERENCES users(id),
                title TEXT NOT NULL,
                description TEXT,
                category TEXT,
                priority INTEGER DEFAULT 3,
                complexity INTEGER DEFAULT 2,
                status TEXT DEFAULT 'PENDING',
                latitude REAL,
                longitude REAL,
                address TEXT,
                assigned_technician_id INTEGER REFERENCES technicians(id),
                estimated_duration INTEGER DEFAULT 60,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                assigned_at DATETIME,
                started_at DATETIME,
                completed_at DATETIME,
                created_by INTEGER REFERENCES users(id)
            );

            CREATE TABLE IF NOT EXISTS routes (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                technician_id INTEGER REFERENCES technicians(id),
                ticket_id INTEGER REFERENCES tickets(id),
                waypoints TEXT,
                total_distance REAL,
                estimated_time REAL,
                traffic_factor REAL DEFAULT 1.0,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                status TEXT DEFAULT 'PLANNED'
            );

            CREATE TABLE IF NOT EXISTS fiber_nodes (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                latitude REAL NOT NULL,
                longitude REAL NOT NULL,
                node_type TEXT,
                status TEXT DEFAULT 'ACTIVE',
                signal_strength REAL,
                last_checked DATETIME
            );

            CREATE TABLE IF NOT EXISTS notifications (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                user_id INTEGER REFERENCES users(id),
                message TEXT,
                type TEXT,
                read INTEGER DEFAULT 0,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS ticket_history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                ticket_id INTEGER REFERENCES tickets(id),
                action TEXT,
                description TEXT,
                user_name TEXT,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS traffic_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                type TEXT NOT NULL,
                description TEXT,
                severity TEXT DEFAULT 'MODERATE',
                latitude REAL NOT NULL,
                longitude REAL NOT NULL,
                radius_km REAL DEFAULT 1.0,
                delay_minutes REAL DEFAULT 10,
                active INTEGER DEFAULT 1,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS pop_monitoring (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                latitude REAL NOT NULL,
                longitude REAL NOT NULL,
                signal_strength REAL DEFAULT 0,
                status TEXT DEFAULT 'ACTIVE',
                detected_at DATETIME,
                restored_at DATETIME
            );
        )";

        char* errMsg = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            cerr << "SQL error: " << errMsg << endl;
            sqlite3_free(errMsg);
        }

        // Migration: ensure tickets.created_by exists on older databases
        sqlite3_exec(db, "ALTER TABLE tickets ADD COLUMN created_by INTEGER DEFAULT NULL", nullptr, nullptr, nullptr);

        // Migration: priority engine columns (auto-computed priority)
        sqlite3_exec(db, "ALTER TABLE tickets ADD COLUMN priority_score INTEGER DEFAULT 0", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "ALTER TABLE tickets ADD COLUMN priority_factors TEXT DEFAULT '{}'", nullptr, nullptr, nullptr);
sqlite3_exec(db, "ALTER TABLE tickets ADD COLUMN evidence_image TEXT DEFAULT NULL", nullptr, nullptr, nullptr);
sqlite3_exec(db, "ALTER TABLE tickets ADD COLUMN problem_image TEXT DEFAULT NULL", nullptr, nullptr, nullptr);
        // Imagens em base64: evidência do técnico (conclusão) e foto do problema (cliente/gestor)
        sqlite3_exec(db, "ALTER TABLE tickets ADD COLUMN evidence_image TEXT DEFAULT NULL", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "ALTER TABLE tickets ADD COLUMN problem_image TEXT DEFAULT NULL", nullptr, nullptr, nullptr);
    }

    void seed_data() {
        // Check if already seeded
        sqlite3_stmt* stmt;
        const char* check = "SELECT COUNT(*) FROM users";
        sqlite3_prepare_v2(db, check, -1, &stmt, nullptr);
        sqlite3_step(stmt);
        int count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        
        if (count > 0) return;

        // Seed users
        const char* users_sql = R"(
            INSERT INTO users (username, password, role, name, email, phone) VALUES
            ('admin', 'admin123', 'admin', 'Administrador AYKO', 'admin@ayko.tech', '(27) 4009-4800'),
            ('cliente1', '123456', 'client', 'João Silva', 'joao@email.com', '(27) 99999-1234'),
            ('cliente2', '123456', 'client', 'Maria Santos', 'maria@email.com', '(27) 99999-5678'),
            ('cliente3', '123456', 'client', 'Empresa XYZ', 'contato@xyz.com', '(27) 3333-4444'),
            ('tech1', 'tech123', 'technician', 'Rafael Augusto Coutinho Vieira', 'rafael.coutinho@ayko.tech', '(27) 99888-1101'),
            ('tech2', 'tech123', 'technician', 'Bruno Henrique Salgado Moraes', 'bruno.salgado@ayko.tech', '(27) 99888-1102'),
            ('tech3', 'tech123', 'technician', 'Diego Fernando Barcelos Tristão', 'diego.barcelos@ayko.tech', '(27) 99888-1103'),
            ('tech4', 'tech123', 'technician', 'Leandro Cassiano Reboredo Xavier', 'leandro.cassiano@ayko.tech', '(27) 99888-1104'),
            ('tech5', 'tech123', 'technician', 'Vagner Emerson Dalpiaz Correa', 'vagner.dalpiaz@ayko.tech', '(27) 99888-1105'),
            ('tech6', 'tech123', 'technician', 'Rodrigo Almeida Bissoli Fagundes', 'rodrigo.bissoli@ayko.tech', '(27) 99888-1106'),
            ('tech7', 'tech123', 'technician', 'Marcos Vinícius Prado Lacerda', 'marcos.lacerda@ayko.tech', '(27) 99888-1201'),
            ('tech8', 'tech123', 'technician', 'Thiago Wanderley Costa Brum', 'thiago.brum@ayko.tech', '(27) 99888-1202'),
            ('tech9', 'tech123', 'technician', 'Felipe André Guimarães Sodré', 'felipe.sodre@ayko.tech', '(27) 99888-1203'),
            ('tech10', 'tech123', 'technician', 'Gustavo Emanuel Piassi Correia', 'gustavo.piassi@ayko.tech', '(27) 99888-1204'),
            ('tech11', 'tech123', 'technician', 'Vinícius Rodrigo Falqueto Nunes', 'vinicius.falqueto@ayko.tech', '(27) 99888-1205'),
            ('tech12', 'tech123', 'technician', 'Anderson Luiz Bittencourt Zilli', 'anderson.zilli@ayko.tech', '(27) 99888-1206'),
            ('tech13', 'tech123', 'technician', 'Kaique Douglas Mendonça Rangel', 'kaique.rangel@ayko.tech', '(27) 99888-1301'),
            ('tech14', 'tech123', 'technician', 'Lucas Gabriel Furtado Espíndula', 'lucas.espindula@ayko.tech', '(27) 99888-1302'),
            ('tech15', 'tech123', 'technician', 'Renan Matheus Cordovil Braga', 'renan.cordovil@ayko.tech', '(27) 99888-1303'),
            ('tech16', 'tech123', 'technician', 'Everton José Cassaro Pimentel', 'everton.pimentel@ayko.tech', '(27) 99888-1304'),
            ('tech17', 'tech123', 'technician', 'Wesley Adriano Bragança Del Duca', 'wesley.braganca@ayko.tech', '(27) 99888-1305'),
            ('tech18', 'tech123', 'technician', 'Igor Fabrício Sarnaglia Louzada', 'igor.sarnaglia@ayko.tech', '(27) 99888-1306');
        )";
        char* errMsg = nullptr;
        sqlite3_exec(db, users_sql, nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);

        // Seed technicians (Corpo Técnico AYKO - 18 técnicos, níveis N3/N2/N1 do PDF simulado)
        // Nível 3 (N3) = skill 5, Nível 2 (N2) = skill 3, Nível 1 (N1) = skill 1
        const char* techs_sql = R"(
            INSERT INTO technicians (user_id, skill_level, specialties, current_status, latitude, longitude, completed_tasks, avg_resolution_time, rating) VALUES
            (5, 5, 'fibra,radio,wifi,servidor,seguranca', 'IDLE', -20.3155, -40.3128, 120, 32.5, 4.9),
            (6, 5, 'fibra,radio,wifi,configuracao', 'IDLE', -20.3200, -40.3050, 98, 36.2, 4.8),
            (7, 5, 'fibra,radio,wifi,servidor', 'IDLE', -20.1550, -40.2900, 132, 30.0, 5.0),
            (8, 5, 'fibra,configuracao,seguranca', 'IDLE', -20.3350, -40.2950, 87, 38.4, 4.7),
            (9, 5, 'fibra,radio,wifi', 'IDLE', -20.3100, -40.3180, 76, 40.9, 4.6),
            (10, 5, 'fibra,radio,wifi,servidor,seguranca', 'IDLE', -20.3250, -40.3080, 104, 34.0, 4.8),
            (11, 3, 'fibra,configuracao', 'IDLE', -20.1570, -40.3010, 52, 48.0, 4.5),
            (12, 3, 'fibra,radio', 'IDLE', -20.3380, -40.2920, 45, 52.0, 4.4),
            (13, 3, 'wifi,configuracao,fibra', 'IDLE', -20.3120, -40.3110, 61, 46.5, 4.6),
            (14, 3, 'fibra,radio,configuracao', 'IDLE', -20.3180, -40.3160, 38, 55.0, 4.3),
            (15, 3, 'fibra,wifi', 'IDLE', -20.1600, -40.3050, 44, 50.3, 4.5),
            (16, 3, 'radio,configuracao,seguranca', 'IDLE', -20.3320, -40.3000, 40, 53.8, 4.4),
            (17, 1, 'wifi', 'IDLE', -20.3210, -40.3090, 12, 65.0, 4.0),
            (18, 1, 'wifi,configuracao', 'IDLE', -20.1560, -40.2950, 9, 70.0, 3.9),
            (19, 1, 'instalacao,wifi', 'IDLE', -20.3340, -40.2980, 15, 62.0, 4.1),
            (20, 1, 'wifi,instalacao', 'IDLE', -20.3140, -40.3130, 8, 68.5, 4.0),
            (21, 1, 'configuracao,wifi', 'IDLE', -20.1620, -40.3080, 11, 66.2, 4.0),
            (22, 1, 'instalacao,wifi', 'IDLE', -20.3260, -40.3020, 14, 63.0, 4.1);
        )";
        sqlite3_exec(db, techs_sql, nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);

        // Seed tickets
        const char* tickets_sql = R"(
            INSERT INTO tickets (client_id, title, description, category, priority, complexity, status, latitude, longitude, address, assigned_technician_id, estimated_duration) VALUES
            (2, 'Internet lenta', 'Cliente reporta lentidão constante na conexão de 200Mbps', 'connectivity', 3, 2, 'PENDING', -20.3180, -40.3100, 'Rua das Flores, 123 - Centro', NULL, 45),
            (2, 'Sem conexão', 'Queda total do serviço há 2 horas', 'outage', 5, 4, 'PENDING', -20.3200, -40.3080, 'Av. Principal, 456 - Praia', NULL, 90),
            (3, 'Troca de equipamento', 'Router apresenta defeito, necessita substituição', 'hardware', 2, 1, 'PENDING', -20.3120, -40.3150, 'Rua do Comércio, 789 - Jardim', NULL, 30),
            (4, 'Instalação nova', 'Nova filial precisa de link dedicado de 500Mbps', 'installation', 4, 5, 'PENDING', -20.3280, -40.3050, 'Av. Empresarial, 1000 - Corporate', NULL, 180),
            (3, 'Intermitência', 'Conexão cai e volta repetidamente', 'connectivity', 3, 3, 'PENDING', -20.3160, -40.3180, 'Rua da Praia, 321 - Orla', NULL, 60),
            (2, 'Wi-Fi não alcança quartos', 'Sinal fraco nos cômodos mais distantes', 'wifi', 2, 2, 'ASSIGNED', -20.3140, -40.3110, 'Rua das Palmeiras, 555 - Vila', 1, 45),
            (4, 'Fibra rompida', 'Obra danificou cabo de fibra na região', 'outage', 5, 5, 'IN_PROGRESS', -20.3220, -40.3060, 'Av. Nova, 2000 - Industrial', 3, 120),
            (3, 'Configuração VPN', 'Necessário configurar acesso remoto para funcionários', 'configuration', 3, 3, 'COMPLETED', -20.3190, -40.3140, 'Rua Comercial, 333 - Centro', 2, 60);
        )";
        sqlite3_exec(db, tickets_sql, nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);

        // Seed fiber nodes
        const char* fiber_sql = R"(
            INSERT INTO fiber_nodes (name, latitude, longitude, node_type, status, signal_strength) VALUES
            ('VTA-01 (POP Vitória-A)', -20.3155, -40.3128, 'POP', 'ACTIVE', -12.5),
            ('Armario-A', -20.3180, -40.3100, 'ARMARIO', 'ACTIVE', -14.2),
            ('Armario-B', -20.3200, -40.3080, 'ARMARIO', 'WARNING', -18.5),
            ('CTO-001', -20.3170, -40.3140, 'CTO', 'ACTIVE', -15.0),
            ('CTO-002', -20.3190, -40.3110, 'CTO', 'ACTIVE', -13.8),
            ('CTO-003', -20.3210, -40.3070, 'CTO', 'CRITICAL', -22.0),
            ('VVA-01 (POP Vila Velha-A)', -20.3350, -40.2950, 'POP', 'ACTIVE', -11.0),
            ('Armario-C', -20.3250, -40.3050, 'ARMARIO', 'ACTIVE', -14.5),
            ('CTO-004', -20.3280, -40.3030, 'CTO', 'INACTIVE', -30.0),
            ('Emenda-001', -20.3230, -40.3060, 'EMENDA', 'CRITICAL', -25.0);
        )";
        sqlite3_exec(db, fiber_sql, nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);
    }

    void seed_traffic() {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM traffic_events", -1, &stmt, nullptr);
        sqlite3_step(stmt);
        int count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        if (count > 0) return;

        const char* traffic_sql = R"(
            INSERT INTO traffic_events (type, description, severity, latitude, longitude, radius_km, delay_minutes) VALUES
            ('accident', 'Colisão na Av. Principal', 'HIGH', -20.3210, -40.3090, 0.8, 12),
            ('roadwork', 'Obras de recapeamento na Av. Nova', 'HIGH', -20.3235, -40.3065, 0.7, 15),
            ('congestion', 'Congestionamento no horário de pico', 'MODERATE', -20.3165, -40.3110, 0.9, 10),
            ('flood', 'Alagamento na Rua da Praia', 'HIGH', -20.3140, -40.3170, 0.6, 10),
            ('event', 'Maratona com bloqueio de vias', 'MODERATE', -20.3270, -40.3040, 1.0, 15),
            ('accident', 'Colisão próxima ao Shopping', 'MODERATE', -20.3100, -40.3180, 0.7, 8),
            ('roadwork', 'Obra na Rua das Palmeiras', 'LOW', -20.3145, -40.3115, 0.5, 6)
        )";
        char* errMsg = nullptr;
        sqlite3_exec(db, traffic_sql, nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);
    }

    void seed_gestor_and_pops() {
        char* errMsg = nullptr;

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM users WHERE username='gestor'", -1, &stmt, nullptr);
        sqlite3_step(stmt);
        int has_gestor = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        if (!has_gestor) {
            const char* gestor_sql = "INSERT INTO users (username, password, role, name, email, phone) VALUES "
                                     "('gestor', 'gestor123', 'gestor', 'Gestor de Operações', 'gestor@ayko.tech', '(27) 4009-4801')";
            sqlite3_exec(db, gestor_sql, nullptr, nullptr, &errMsg);
            if (errMsg) sqlite3_free(errMsg);
        }

        sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM pop_monitoring", -1, &stmt, nullptr);
        sqlite3_step(stmt);
        int has_pops = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        if (!has_pops) {
            // 5 Pontos de Presença (POPs) do PDF simulado do Corpo Técnico AYKO
            const char* pops_sql = "INSERT INTO pop_monitoring (name, latitude, longitude, signal_strength, status, detected_at) VALUES "
                                   "('SEA-01 - Pop Serra – Setor A', -20.1550, -40.2900, -14.5, 'ACTIVE', NULL),"
                                   "('SEA-02 - Pop Serra – Setor B', -20.1700, -40.3100, -12.0, 'ACTIVE', NULL),"
                                   "('VVA-01 - Pop Vila Velha – Setor A', -20.3350, -40.2950, -16.5, 'WARNING', NULL),"
                                   "('VTA-01 - Pop Vitória – Setor A', -20.3155, -40.3128, -28.5, 'DOWN', datetime('now', '-8 minutes')),"
                                   "('VTA-02 - Pop Vitória – Setor B', -20.3100, -40.3150, -13.5, 'ACTIVE', NULL)";
            sqlite3_exec(db, pops_sql, nullptr, nullptr, &errMsg);
            if (errMsg) sqlite3_free(errMsg);
        }
    }

    void seed_priority_data() {
        // Runs AFTER seed_data so rows exist (idempotent migrations)
        char* errMsg = nullptr;
        // Mark corporate/VIP clients
        sqlite3_exec(db, "UPDATE users SET tier='business' WHERE username='cliente3'", nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);
        // Simple/commerce client (empresa simples ou comércio)
        sqlite3_exec(db, "INSERT OR IGNORE INTO users (username, password, role, name, email, phone, tier) VALUES "
                         "('comercio1', '123456', 'client', 'Padaria Central', 'padaria@email.com', '(27) 99999-8888', 'commerce')",
                     nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);
        // Seed affected client counts on critical fiber nodes
        sqlite3_exec(db, "UPDATE fiber_nodes SET connected_clients=28 WHERE name='CTO-003'", nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);
        sqlite3_exec(db, "UPDATE fiber_nodes SET connected_clients=12 WHERE name='CTO-004'", nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);
        sqlite3_exec(db, "UPDATE fiber_nodes SET connected_clients=19 WHERE name='Emenda-001'", nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);
        sqlite3_exec(db, "UPDATE fiber_nodes SET connected_clients=6 WHERE name='Armario-B'", nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);
        // POP also carries affected clients
        sqlite3_exec(db, "UPDATE fiber_nodes SET connected_clients=45 WHERE name='VTA-01 (POP Vitória-A)'", nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);
        sqlite3_exec(db, "UPDATE fiber_nodes SET connected_clients=34 WHERE name='VVA-01 (POP Vila Velha-A)'", nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);
    }

    sqlite3* get_db() { return db; }
    mutex& get_mutex() { return db_mutex; }

    // Execute query and return JSON
    string query_to_json(const string& sql) {
        lock_guard<mutex> lock(db_mutex);
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return "{\"error\":\"" + string(sqlite3_errmsg(db)) + "\"}";
        }

        string result = "[";
        int col_count = sqlite3_column_count(stmt);
        bool first = true;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!first) result += ",";
            first = false;
            result += "{";
            for (int i = 0; i < col_count; i++) {
                if (i > 0) result += ",";
                result += "\"" + string(sqlite3_column_name(stmt, i)) + "\":";
                int type = sqlite3_column_type(stmt, i);
                if (type == SQLITE_NULL) {
                    result += "null";
                } else if (type == SQLITE_INTEGER) {
                    result += to_string(sqlite3_column_int64(stmt, i));
                } else if (type == SQLITE_FLOAT) {
                    double val = sqlite3_column_double(stmt, i);
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%.6f", val);
                    result += buf;
                } else {
                    const char* val = (const char*)sqlite3_column_text(stmt, i);
                    string s = val ? val : "";
                    // Escape JSON string content (quotes, backslashes, control chars)
                    string escaped;
                    for (char c : s) {
                        if (c == '"') escaped += "\\\"";
                        else if (c == '\\') escaped += "\\\\";
                        else if (c == '\n') escaped += "\\n";
                        else if (c == '\r') escaped += "\\r";
                        else if (c == '\t') escaped += "\\t";
                        else escaped += c;
                    }
                    result += "\"" + escaped + "\"";
                }
            }
            result += "}";
        }
        result += "]";
        sqlite3_finalize(stmt);
        return result;
    }

    // Execute insert/update/delete
    bool execute(const string& sql) {
        lock_guard<mutex> lock(db_mutex);
        char* errMsg = nullptr;
        int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            if (errMsg) sqlite3_free(errMsg);
            return false;
        }
        return true;
    }

    // Execute insert and return last id
    long long execute_insert(const string& sql) {
        lock_guard<mutex> lock(db_mutex);
        char* errMsg = nullptr;
        int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            if (errMsg) sqlite3_free(errMsg);
            return -1;
        }
        return sqlite3_last_insert_rowid(db);
    }

    // Query single value
    string query_scalar(const string& sql) {
        lock_guard<mutex> lock(db_mutex);
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return "";
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* val = (const char*)sqlite3_column_text(stmt, 0);
            string result = val ? val : "";
            sqlite3_finalize(stmt);
            return result;
        }
        sqlite3_finalize(stmt);
        return "";
    }
};

// ==================== GLOBAL STATE ====================

Database* g_db = nullptr;
map<string, int> g_sessions; // session_token -> user_id

// ==================== HELPER FUNCTIONS ====================

string generate_session_token() {
    static random_device rd;
    static mt19937 gen(rd());
    static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    uniform_int_distribution<> dis(0, sizeof(chars) - 2);
    string token;
    for (int i = 0; i < 32; i++) {
        token += chars[dis(gen)];
    }
    return token;
}

string url_decode(const string& str) {
    string result;
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int hex;
            sscanf(str.substr(i + 1, 2).c_str(), "%x", &hex);
            result += (char)hex;
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

map<string, string> parse_form_data(const string& body) {
    map<string, string> params;
    stringstream ss(body);
    string pair;
    while (getline(ss, pair, '&')) {
        size_t pos = pair.find('=');
        if (pos != string::npos) {
            params[pair.substr(0, pos)] = url_decode(pair.substr(pos + 1));
        }
    }
    return params;
}

map<string, string> parse_json(const string& json) {
    map<string, string> result;
    size_t pos = 0;
    while (pos < json.length()) {
        size_t key_start = json.find('"', pos);
        if (key_start == string::npos) break;
        size_t key_end = json.find('"', key_start + 1);
        if (key_end == string::npos) break;
        string key = json.substr(key_start + 1, key_end - key_start - 1);
        
        size_t val_start = json.find(':', key_end);
        if (val_start == string::npos) break;
        val_start++;
        while (val_start < json.length() && json[val_start] == ' ') val_start++;
        
        if (json[val_start] == '"') {
            size_t val_end = json.find('"', val_start + 1);
            if (val_end == string::npos) break;
            result[key] = json.substr(val_start + 1, val_end - val_start - 1);
            pos = val_end + 1;
        } else {
            size_t val_end = json.find_first_of(",}", val_start);
            if (val_end == string::npos) break;
            string val = json.substr(val_start, val_end - val_start);
            while (!val.empty() && val.back() == ' ') val.pop_back();
            result[key] = val;
            pos = val_end;
        }
    }
    return result;
}

string get_current_timestamp() {
    auto now = chrono::system_clock::now();
    time_t t = chrono::system_clock::to_time_t(now);
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return string(buf);
}

void add_history(Database* db, int ticket_id, const string& action, const string& description, const string& user_name) {
    string clean_desc = description;
    size_t pos;
    while ((pos = clean_desc.find("'")) != string::npos) clean_desc.replace(pos, 1, "''");
    string sql = "INSERT INTO ticket_history (ticket_id, action, description, user_name) VALUES (" +
                 to_string(ticket_id) + ", '" + action + "', '" + clean_desc + "', '" + user_name + "')";
    db->execute(sql);
}

// ==================== TRAFFIC SIMULATOR ====================

struct TrafficEvent {
    int id;
    string type;
    string description;
    string severity;
    double latitude;
    double longitude;
    double radius_km;
    double delay_minutes;
};

class TrafficSimulator {
public:
    static vector<TrafficEvent> get_active_events(Database* db) {
        vector<TrafficEvent> events;
        sqlite3_stmt* stmt;
        const char* sql = "SELECT id, type, description, severity, latitude, longitude, radius_km, delay_minutes "
                          "FROM traffic_events WHERE active = 1";
        sqlite3_prepare_v2(db->get_db(), sql, -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            TrafficEvent e;
            e.id = sqlite3_column_int(stmt, 0);
            e.type = (const char*)sqlite3_column_text(stmt, 1);
            e.description = (const char*)sqlite3_column_text(stmt, 2);
            e.severity = (const char*)sqlite3_column_text(stmt, 3);
            e.latitude = sqlite3_column_double(stmt, 4);
            e.longitude = sqlite3_column_double(stmt, 5);
            e.radius_km = sqlite3_column_double(stmt, 6);
            e.delay_minutes = sqlite3_column_double(stmt, 7);
            events.push_back(e);
        }
        sqlite3_finalize(stmt);
        return events;
    }

    static double point_segment_distance_km(double pLat, double pLon,
                                            double aLat, double aLon,
                                            double bLat, double bLon) {
        double midLat = (aLat + bLat) / 2.0;
        double cosLat = cos(midLat * M_PI / 180.0);
        double px = pLon * cosLat * 111.320;
        double py = pLat * 110.574;
        double ax = aLon * cosLat * 111.320;
        double ay = aLat * 110.574;
        double bx = bLon * cosLat * 111.320;
        double by = bLat * 110.574;
        double dx = bx - ax, dy = by - ay;
        double len2 = dx * dx + dy * dy;
        double t = (len2 > 0) ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
        t = max(0.0, min(1.0, t));
        double cx = ax + t * dx, cy = ay + t * dy;
        return sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
    }

    static double severity_multiplier(const string& severity) {
        if (severity == "HIGH") return 1.6;
        if (severity == "MODERATE") return 1.2;
        return 1.0;
    }

    static double route_delay(Database* db, double lat1, double lon1, double lat2, double lon2) {
        auto events = get_active_events(db);
        double total = 0.0;
        for (auto& e : events) {
            double d = point_segment_distance_km(e.latitude, e.longitude, lat1, lon1, lat2, lon2);
            if (d <= e.radius_km) {
                double mult = severity_multiplier(e.severity);
                double proximity = 1.0 - (e.radius_km > 0 ? d / e.radius_km : 0);
                total += e.delay_minutes * mult * proximity;
            }
        }
        return min(total, 45.0);
    }

    static string affected_events_json(Database* db, double lat1, double lon1, double lat2, double lon2) {
        auto events = get_active_events(db);
        stringstream out;
        out << "[";
        bool first = true;
        for (auto& e : events) {
            double d = point_segment_distance_km(e.latitude, e.longitude, lat1, lon1, lat2, lon2);
            if (d <= e.radius_km) {
                if (!first) out << ",";
                first = false;
                out << "{\"id\":" << e.id
                    << ",\"type\":\"" << e.type << "\""
                    << ",\"description\":\"" << e.description << "\""
                    << ",\"severity\":\"" << e.severity << "\""
                    << ",\"latitude\":" << fixed << setprecision(6) << e.latitude
                    << ",\"longitude\":" << e.longitude
                    << ",\"delay_minutes\":" << e.delay_minutes << "}";
            }
        }
        out << "]";
        return out.str();
    }

    static string events_to_json(Database* db) {
        auto events = get_active_events(db);
        stringstream out;
        out << "[";
        for (size_t i = 0; i < events.size(); i++) {
            if (i > 0) out << ",";
            out << "{\"id\":" << events[i].id
                << ",\"type\":\"" << events[i].type << "\""
                << ",\"description\":\"" << events[i].description << "\""
                << ",\"severity\":\"" << events[i].severity << "\""
                << ",\"latitude\":" << fixed << setprecision(6) << events[i].latitude
                << ",\"longitude\":" << events[i].longitude
                << ",\"radius_km\":" << events[i].radius_km
                << ",\"delay_minutes\":" << events[i].delay_minutes
                << "}";
        }
        out << "]";
        return out.str();
    }
};

// ==================== SKILL MATCHER ====================

struct MatchResult {
    int technician_id;
    double score;
    double delay_minutes;
    string reason;
};

class SkillMatcher {
public:
    static vector<MatchResult> find_best_match(Database* db, int complexity, double lat, double lon, const string& category) {
        vector<MatchResult> results;
        
        string sql = "SELECT t.id, t.skill_level, t.specialties, t.current_status, "
                     "t.latitude, t.longitude, t.rating, t.completed_tasks, "
                     "t.avg_resolution_time, u.name "
                     "FROM technicians t JOIN users u ON t.user_id = u.id "
                     "WHERE t.current_status = 'IDLE'";
        
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db->get_db(), sql.c_str(), -1, &stmt, nullptr);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            MatchResult match;
            match.technician_id = sqlite3_column_int(stmt, 0);
            int skill_level = sqlite3_column_int(stmt, 1);
            const char* specialties = (const char*)sqlite3_column_text(stmt, 2);
            double tech_lat = sqlite3_column_double(stmt, 4);
            double tech_lon = sqlite3_column_double(stmt, 5);
            double rating = sqlite3_column_double(stmt, 6);
            int completed = sqlite3_column_int(stmt, 7);
            double avg_time = sqlite3_column_double(stmt, 8);
            
            // Calculate distance (haversine)
            double dist = haversine(tech_lat, tech_lon, lat, lon);

            // Traffic/obstacles delay along the route tech -> client
            double delay = TrafficSimulator::route_delay(db, tech_lat, tech_lon, lat, lon);
            match.delay_minutes = delay;

            // Calculate score (0-100)
            double score = 0;
            
            // Skill match (40% weight)
            double skill_match = (skill_level >= complexity) ? 40.0 : (skill_level / (double)complexity) * 40.0;
            score += skill_match;
            
            // Proximity (30% weight) - based on effective ETA (distance + traffic/obstacles delay)
            double base_minutes = (dist / 30.0) * 60.0;   // avg 30 km/h urban
            double effective_minutes = base_minutes + delay;
            double prox_score = max(0.0, 30.0 - (effective_minutes * 0.8));
            score += prox_score;
            
            // Rating (15% weight)
            score += (rating / 5.0) * 15.0;
            
            // Experience (15% weight) - based on completed tasks
            double exp_score = min(15.0, completed / 10.0);
            score += exp_score;
            
            match.score = score;
            
            // Generate reason
            stringstream reason;
            reason << "Nível " << skill_level << "/5 | Distância: " << fixed << setprecision(1)
                   << dist << "km | ETA ~" << setprecision(0) << effective_minutes
                   << "min (+" << setprecision(1) << delay << "min eventos) | Rating: "
                   << setprecision(1) << rating << "★";
            match.reason = reason.str();
            
            results.push_back(match);
        }
        sqlite3_finalize(stmt);
        
        // Sort by score descending
        sort(results.begin(), results.end(), [](const MatchResult& a, const MatchResult& b) {
            return a.score > b.score;
        });
        
        return results;
    }

private:
    static double haversine(double lat1, double lon1, double lat2, double lon2) {
        const double R = 6371; // Earth radius in km
        double dlat = (lat2 - lat1) * M_PI / 180;
        double dlon = (lon2 - lon1) * M_PI / 180;
        double a = sin(dlat/2) * sin(dlat/2) +
                   cos(lat1 * M_PI / 180) * cos(lat2 * M_PI / 180) *
                   sin(dlon/2) * sin(dlon/2);
        double c = 2 * atan2(sqrt(a), sqrt(1-a));
        return R * c;
    }
};

// ==================== ROUTE OPTIMIZER ====================

class RouteOptimizer {
public:
    static string optimize_route(Database* db, int technician_id, vector<int> ticket_ids) {
        // Technician origin
        double origin_lat = -20.3155, origin_lon = -40.3128;
        {
            sqlite3_stmt* stmt;
            string sql = "SELECT latitude, longitude FROM technicians WHERE id = " + to_string(technician_id);
            sqlite3_prepare_v2(db->get_db(), sql.c_str(), -1, &stmt, nullptr);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                origin_lat = sqlite3_column_double(stmt, 0);
                origin_lon = sqlite3_column_double(stmt, 1);
            }
            sqlite3_finalize(stmt);
        }

        double prev_lat = origin_lat, prev_lon = origin_lon;
        double total_delay = 0.0, total_minutes = 0.0;

        stringstream result;
        result << fixed << setprecision(6)
               << "{\"technician_id\":" << technician_id
               << ",\"origin\":{\"latitude\":" << origin_lat
               << ",\"longitude\":" << origin_lon << "}"
               << ",\"waypoints\":[";

        for (size_t i = 0; i < ticket_ids.size(); i++) {
            if (i > 0) result << ",";
            int tid = ticket_ids[i];
            sqlite3_stmt* stmt;
            string sql = "SELECT latitude, longitude, address, priority, title FROM tickets WHERE id = " + to_string(tid);
            double lat = 0, lon = 0, priority = 0;
            string address, title;
            sqlite3_prepare_v2(db->get_db(), sql.c_str(), -1, &stmt, nullptr);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                lat = sqlite3_column_double(stmt, 0);
                lon = sqlite3_column_double(stmt, 1);
                address = (const char*)sqlite3_column_text(stmt, 2);
                priority = sqlite3_column_double(stmt, 3);
                title = (const char*)sqlite3_column_text(stmt, 4);
            }
            sqlite3_finalize(stmt);

            double dist = haversine(prev_lat, prev_lon, lat, lon);
            double delay = TrafficSimulator::route_delay(db, prev_lat, prev_lon, lat, lon);
            double minutes = (dist / 30.0) * 60.0 + delay;
            total_delay += delay;
            total_minutes += minutes;

            string events_json = TrafficSimulator::affected_events_json(db, prev_lat, prev_lon, lat, lon);

            result << "{\"ticket_id\":" << tid
                   << ",\"title\":\"" << title << "\""
                   << ",\"latitude\":" << lat
                   << ",\"longitude\":" << lon
                   << ",\"address\":\"" << address << "\""
                   << ",\"priority\":" << setprecision(0) << priority
                   << ",\"distance_km\":" << setprecision(2) << dist
                   << ",\"delay_minutes\":" << setprecision(1) << delay
                   << ",\"eta_minutes\":" << setprecision(1) << minutes
                   << ",\"traffic_events\":" << events_json << "}";

            prev_lat = lat; prev_lon = lon;
        }

        result << setprecision(1)
               << "],\"total_delay_minutes\":" << total_delay
               << ",\"total_eta_minutes\":" << total_minutes
               << ",\"traffic_events\":" << TrafficSimulator::events_to_json(db) << "}";
        return result.str();
    }

    static double calculate_eta(double distance_km, const string& traffic_condition) {
        double base_speed = 30.0; // km/h base
        if (traffic_condition == "heavy") base_speed = 15.0;
        else if (traffic_condition == "moderate") base_speed = 22.0;
        return (distance_km / base_speed) * 60.0; // minutes
    }

private:
    static double haversine(double lat1, double lon1, double lat2, double lon2) {
        const double R = 6371;
        double dlat = (lat2 - lat1) * M_PI / 180;
        double dlon = (lon2 - lon1) * M_PI / 180;
        double a = sin(dlat/2) * sin(dlat/2) +
                   cos(lat1 * M_PI / 180) * cos(lat2 * M_PI / 180) *
                   sin(dlon/2) * sin(dlon/2);
        double c = 2 * atan2(sqrt(a), sqrt(1-a));
        return R * c;
    }
};

// ==================== FIBER TRIANGULATOR ====================

class FiberTriangulator {
public:
    static string triangulate_fault(double lat, double lon) {
        // Simulate fiber fault triangulation
        // In real implementation, this would use OTDR data and fiber topology
        stringstream result;
        result << "{\"fault_location\":{\"latitude\":" << fixed << setprecision(6) 
               << (lat + 0.002) << ",\"longitude\":" << (lon - 0.001)
               << "},\"confidence\":87.5,\"distance_from_pop_m\":342,\"nearest_node\":\"CTO-003\","
               << "\"suggested_action\":\"Verificar emenda na caixa de passagem\","
               << "\"signal_loss_dB\":-22.4}";
        return result.str();
    }
};

// ==================== POP OUTAGE DETECTION ====================

class POPMonitor {
public:
    static string status_to_json(Database* db) {
        stringstream out;
        out << "[";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db->get_db(), "SELECT id, name, latitude, longitude, signal_strength, status, detected_at, restored_at FROM pop_monitoring", -1, &stmt, nullptr);
        bool first = true;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            string name = (const char*)sqlite3_column_text(stmt, 1);
            double lat = sqlite3_column_double(stmt, 2);
            double lon = sqlite3_column_double(stmt, 3);
            double signal = sqlite3_column_double(stmt, 4);
            string status = (const char*)sqlite3_column_text(stmt, 5);
            const char* detected = (const char*)sqlite3_column_text(stmt, 6);
            const char* restored = (const char*)sqlite3_column_text(stmt, 7);

            // Recompute status from signal thresholds (simulated telemetry)
            if (signal < -20) status = "DOWN";
            else if (signal < -15) status = "WARNING";
            else status = "ACTIVE";

            if (!first) out << ",";
            first = false;
            out << "{\"id\":" << id
                << ",\"name\":\"" << name << "\""
                << ",\"latitude\":" << fixed << setprecision(6) << lat
                << ",\"longitude\":" << lon
                << ",\"signal_strength\":" << setprecision(1) << signal
                << ",\"status\":\"" << status << "\""
                << ",\"detected_at\":\"" << (detected ? detected : "") << "\""
                << ",\"restored_at\":\"" << (restored ? restored : "") << "\""
                << ",\"affected_tickets\":[";

            // Affected clients/tickets within 2km of a down POP
            if (status == "DOWN" || status == "WARNING") {
                sqlite3_stmt* t;
                string tsql = "SELECT t.id, t.title, u.name, t.latitude, t.longitude FROM tickets t "
                              "JOIN users u ON t.client_id = u.id "
                              "WHERE t.status != 'COMPLETED'";
                sqlite3_prepare_v2(db->get_db(), tsql.c_str(), -1, &t, nullptr);
                bool tfirst = true;
                while (sqlite3_step(t) == SQLITE_ROW) {
                    double tlat = sqlite3_column_double(t, 3);
                    double tlon = sqlite3_column_double(t, 4);
                    if (POPMonitor::haversine(lat, lon, tlat, tlon) <= 2.0) {
                        if (!tfirst) out << ",";
                        tfirst = false;
                        out << "{\"id\":" << sqlite3_column_int(t, 0)
                            << ",\"title\":\"" << (const char*)sqlite3_column_text(t, 1) << "\""
                            << ",\"client\":\"" << (const char*)sqlite3_column_text(t, 2) << "\"}";
                    }
                }
                sqlite3_finalize(t);
            }
            out << "]}";
        }
        sqlite3_finalize(stmt);
        out << "]";
        return out.str();
    }

private:
    static double haversine(double lat1, double lon1, double lat2, double lon2) {
        const double R = 6371;
        double dlat = (lat2 - lat1) * M_PI / 180;
        double dlon = (lon2 - lon1) * M_PI / 180;
        double a = sin(dlat/2) * sin(dlat/2) +
                   cos(lat1 * M_PI / 180) * cos(lat2 * M_PI / 180) *
                   sin(dlon/2) * sin(dlon/2);
        double c = 2 * atan2(sqrt(a), sqrt(1-a));
        return R * c;
    }
};

// ==================== PRIORITY CALCULATOR ====================
// Algoritmo transparente de prioridade de chamados.
// O score é a soma ponderada de fatores objetivos e públicos:
//   categoria (0-40) + complexidade (0-20) + urgência relatada (0-12)
//   + tempo de espera (0-15)
//   + porte do cliente (Residencial=100, Empresa simples/comércio=200,
//     Empresa importante=250, Backbone=300)
//   + clientes afetados (somatório > 2 clientes => +250)
// Mapeamento p/ prioridade 1-5: [450+]=5, [350-449]=4, [250-349]=3,
//                                [150-249]=2, [0-149]=1

class PriorityCalculator {
public:
    struct Factors {
        string category;
        string tier;
        int complexity = 0;
        int urgency = 0;
        int age_hours = 0;
        int affected = 0;
        int backbone = 0;
        int category_score = 0;
        int complexity_score = 0;
        int urgency_score = 0;
        int age_score = 0;
        int tier_score = 0;
        int affected_score = 0;
        int backbone_score = 0;
        int total = 0;
        int priority = 0;
    };

    static int category_weight(const string& category) {
        if (category == "outage") return 40;
        if (category == "connectivity") return 25;
        if (category == "hardware") return 20;
        if (category == "wifi") return 15;
        if (category == "installation") return 10;
        if (category == "configuration") return 5;
        return 10;
    }

    static int priority_from_score(int score) {
        if (score >= 450) return 5;
        if (score >= 350) return 4;
        if (score >= 250) return 3;
        if (score >= 150) return 2;
        return 1;
    }

    static Factors compute(Database* db, int ticket_id) {
        Factors f;
        sqlite3_stmt* stmt;
        string sql = "SELECT t.category, t.priority, t.complexity, t.latitude, t.longitude, "
                     "COALESCE(t.created_at,''), COALESCE(u.tier,'standard') "
                     "FROM tickets t JOIN users u ON t.client_id = u.id "
                     "WHERE t.id = " + to_string(ticket_id);
        sqlite3_prepare_v2(db->get_db(), sql.c_str(), -1, &stmt, nullptr);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            f.category = (const char*)sqlite3_column_text(stmt, 0);
            f.urgency = sqlite3_column_int(stmt, 1);
            f.complexity = sqlite3_column_int(stmt, 2);
            double lat = sqlite3_column_double(stmt, 3);
            double lon = sqlite3_column_double(stmt, 4);
            string created_at = (const char*)sqlite3_column_text(stmt, 5);
            f.tier = (const char*)sqlite3_column_text(stmt, 6);
            f.age_hours = compute_age_hours(db, created_at);
            // Network impact: soma de clientes afetados + detecção de backbone
            compute_network_impact(db, lat, lon, f.affected, f.backbone);
        }
        sqlite3_finalize(stmt);

        f.category_score = category_weight(f.category);
        f.complexity_score = (max(1, min(5, f.complexity)) - 1) * 5;
        f.urgency_score = (max(1, min(5, f.urgency)) - 1) * 3;
        f.age_score = min(f.age_hours, 15);

        // Porte do cliente / backbone (pesos altos e públicos)
        if (f.tier == "vip" || f.tier == "business") f.tier_score = 250;
        else if (f.tier == "commerce") f.tier_score = 200;
        else f.tier_score = 100;

        f.backbone_score = f.backbone ? 300 : 0;
        f.affected_score = f.affected > 2 ? 250 : 0;

        f.total = f.category_score + f.complexity_score + f.urgency_score +
                  f.age_score + f.tier_score + f.backbone_score + f.affected_score;
        f.priority = priority_from_score(f.total);
        return f;
    }

    static string factors_to_json(const Factors& f) {
        stringstream out;
        out << "{\"category\":\"" << f.category << "\""
            << ",\"category_score\":" << f.category_score
            << ",\"complexity\":" << f.complexity
            << ",\"complexity_score\":" << f.complexity_score
            << ",\"urgency\":" << f.urgency
            << ",\"urgency_score\":" << f.urgency_score
            << ",\"tier\":\"" << f.tier << "\""
            << ",\"tier_score\":" << f.tier_score
            << ",\"age_hours\":" << f.age_hours
            << ",\"age_score\":" << f.age_score
            << ",\"affected\":" << f.affected
            << ",\"affected_score\":" << f.affected_score
            << ",\"backbone\":" << f.backbone
            << ",\"backbone_score\":" << f.backbone_score
            << ",\"score\":" << f.total
            << ",\"priority\":" << f.priority
            << "}";
        return out.str();
    }

    // Recalcula e persiste prioridade de todos os chamados ativos
    static void recompute_all(Database* db) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db->get_db(), "SELECT id FROM tickets WHERE status != 'COMPLETED'", -1, &stmt, nullptr);
        vector<int> ids;
        while (sqlite3_step(stmt) == SQLITE_ROW) ids.push_back(sqlite3_column_int(stmt, 0));
        sqlite3_finalize(stmt);
        for (int id : ids) {
            Factors f = compute(db, id);
            string factors = factors_to_json(f);
            // escape single quotes (JSON uses double quotes, safe)
            string sql = "UPDATE tickets SET priority=" + to_string(f.priority) +
                         ", priority_score=" + to_string(f.total) +
                         ", priority_factors='" + factors + "' WHERE id=" + to_string(id);
            db->execute(sql);
        }
    }

private:
    static int compute_age_hours(Database* db, const string& created_at) {
        if (created_at.empty()) return 0;
        string hours = db->query_scalar(
            "SELECT CAST(ROUND((julianday('now') - julianday('" + created_at + "')) * 24, 0) AS INTEGER)");
        if (hours.empty()) return 0;
        return max(0, stoi(hours));
    }

    // Soma de clientes afetados (nós críticos + POPs fora do ar) a até 2 km
    // e detecção de incidente de backbone (POP principal fora do ar / nó POP crítico)
    static void compute_network_impact(Database* db, double lat, double lon,
                                       int& affected, int& backbone) {
        affected = 0;
        backbone = 0;
        if (lat == 0 && lon == 0) return;

        sqlite3_stmt* stmt;
        // Nós de fibra críticos/inativos
        sqlite3_prepare_v2(db->get_db(),
            "SELECT latitude, longitude, connected_clients, node_type FROM fiber_nodes "
            "WHERE status IN ('CRITICAL','INACTIVE')", -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            double nlat = sqlite3_column_double(stmt, 0);
            double nlon = sqlite3_column_double(stmt, 1);
            int clients = sqlite3_column_int(stmt, 2);
            string node_type = (const char*)sqlite3_column_text(stmt, 3);
            if (haversine(lat, lon, nlat, nlon) <= 2.0) {
                affected += clients;
                if (node_type == "POP") backbone = 1;
            }
        }
        sqlite3_finalize(stmt);

        // POPs fora do ar (backbone) na região
        sqlite3_prepare_v2(db->get_db(),
            "SELECT latitude, longitude FROM pop_monitoring WHERE status='DOWN'", -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            double nlat = sqlite3_column_double(stmt, 0);
            double nlon = sqlite3_column_double(stmt, 1);
            if (haversine(lat, lon, nlat, nlon) <= 2.0) backbone = 1;
        }
        sqlite3_finalize(stmt);
    }

    static double haversine(double lat1, double lon1, double lat2, double lon2) {
        const double R = 6371;
        double dlat = (lat2 - lat1) * M_PI / 180;
        double dlon = (lon2 - lon1) * M_PI / 180;
        double a = sin(dlat/2) * sin(dlat/2) +
                   cos(lat1 * M_PI / 180) * cos(lat2 * M_PI / 180) *
                   sin(dlon/2) * sin(dlon/2);
        double c = 2 * atan2(sqrt(a), sqrt(1-a));
        return R * c;
    }
};

// ==================== SERVER SETUP ====================

static string g_base = "";

static string get_exe_dir() {
#ifdef _WIN32
    char buf[1024];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
    string path(buf, n);
    size_t pos = path.find_last_of("\\/");
    return (pos == string::npos) ? "" : path.substr(0, pos + 1);
#else
    char buf[1024];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    string path(buf);
    size_t pos = path.find_last_of('/');
    return (pos == string::npos) ? "" : path.substr(0, pos + 1);
#endif
}

static string resolve_path(const string& rel) {
    return g_base.empty() ? rel : g_base + rel;
}

static string read_file(const string& path) {
    ifstream file(path, ios::binary);
    if (!file) return "";
    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static crow::response serve_static(const string& path, const string& content_type) {
    string content;
    string rel = path;
    if (rel.rfind("public/", 0) == 0) rel = rel.substr(7);
    auto it = embedded_assets.find(rel);
    if (it != embedded_assets.end()) {
        content = it->second;
    } else {
        content = read_file(resolve_path(path));
    }
    if (content.empty()) {
        return crow::response(404, "Not found");
    }
    crow::response res(content);
    res.set_header("Content-Type", content_type);
    return res;
}

int main() {
    crow::SimpleApp app;

    // Initialize database
    g_base = get_exe_dir();
    g_db = new Database(resolve_path("ayko.db"));

    // Compute priorities for all active tickets (seed + existing)
    PriorityCalculator::recompute_all(g_db);

    // Background thread: refresh priorities (aging factor) periodically
    thread priority_thread([]() {
        while (true) {
            this_thread::sleep_for(chrono::seconds(30));
            if (g_db) PriorityCalculator::recompute_all(g_db);
        }
    });
    priority_thread.detach();

    // ==================== STATIC FILE SERVING ====================

    CROW_ROUTE(app, "/")([]() {
        return serve_static("public/index.html", "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/admin")([]() {
        return serve_static("public/admin/index.html", "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/client")([]() {
        return serve_static("public/client/index.html", "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/technician")([]() {
        return serve_static("public/tecnico/index.html", "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/tecnico")([]() {
        return serve_static("public/tecnico/index.html", "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/gestor")([]() {
        return serve_static("public/gestor/index.html", "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/gestor/<path>")([](string filename) {
        return serve_static("public/gestor/" + filename, "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/assets/css/<path>")([](string filename) {
        return serve_static("public/assets/css/" + filename, "text/css; charset=utf-8");
    });

    CROW_ROUTE(app, "/assets/js/<path>")([](string filename) {
        return serve_static("public/assets/js/" + filename, "application/javascript; charset=utf-8");
    });

    CROW_ROUTE(app, "/client/<path>")([](string filename) {
        return serve_static("public/client/" + filename, "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/admin/<path>")([](string filename) {
        return serve_static("public/admin/" + filename, "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/tecnico/<path>")([](string filename) {
        return serve_static("public/tecnico/" + filename, "text/html; charset=utf-8");
    });

    // ==================== AUTH ENDPOINTS ====================

    CROW_ROUTE(app, "/api/auth/login").methods("POST"_method)([](const crow::request& req) {
        auto params = parse_json(req.body);
        string username = params["username"];
        string password = params["password"];

        string sql = "SELECT id, role, name FROM users WHERE username = '" +
                     username + "' AND password = '" + password + "'";
        string result = g_db->query_to_json(sql);

        if (result == "[]") {
            return crow::response(401, "{\"error\":\"Invalid credentials\"}");
        }

        string token = generate_session_token();
        string user_id = g_db->query_scalar("SELECT id FROM users WHERE username = '" + username + "'");
        g_sessions[token] = stoi(user_id);

        string inner = result.substr(2, result.length() - 4);
        string response = "{\"token\":\"" + token + "\"," + inner + "}";
        crow::response res(response);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    CROW_ROUTE(app, "/api/auth/logout").methods("POST"_method)([](const crow::request& req) {
        auto params = parse_json(req.body);
        string token = params["token"];
        g_sessions.erase(token);
        return crow::response(200, "{\"success\":true}");
    });

    // ==================== DASHBOARD STATS ====================

    CROW_ROUTE(app, "/api/dashboard/stats")([](const crow::request& req) {
        string stats = "{";
        stats += "\"total_tickets\":" + g_db->query_scalar("SELECT COUNT(*) FROM tickets") + ",";
        stats += "\"pending_tickets\":" + g_db->query_scalar("SELECT COUNT(*) FROM tickets WHERE status='PENDING'") + ",";
        stats += "\"assigned_tickets\":" + g_db->query_scalar("SELECT COUNT(*) FROM tickets WHERE status='ASSIGNED'") + ",";
        stats += "\"in_progress_tickets\":" + g_db->query_scalar("SELECT COUNT(*) FROM tickets WHERE status='IN_PROGRESS'") + ",";
        stats += "\"completed_tickets\":" + g_db->query_scalar("SELECT COUNT(*) FROM tickets WHERE status='COMPLETED'") + ",";
        stats += "\"active_technicians\":" + g_db->query_scalar("SELECT COUNT(*) FROM technicians WHERE current_status != 'IDLE'") + ",";
        stats += "\"idle_technicians\":" + g_db->query_scalar("SELECT COUNT(*) FROM technicians WHERE current_status='IDLE'") + ",";
        stats += "\"critical_nodes\":" + g_db->query_scalar("SELECT COUNT(*) FROM fiber_nodes WHERE status='CRITICAL'") + ",";
        stats += "\"pops_down\":" + g_db->query_scalar("SELECT COUNT(*) FROM pop_monitoring WHERE status='DOWN' OR signal_strength < -20") + ",";
        stats += "\"traffic_events\":" + g_db->query_scalar("SELECT COUNT(*) FROM traffic_events WHERE active=1") + ",";
        stats += "\"avg_resolution_time\":" + g_db->query_scalar("SELECT COALESCE(ROUND(AVG(avg_resolution_time),1),0) FROM technicians");
        stats += "}";

        crow::response res(stats);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    // ==================== PRIORITY ALGORITHM (público) ====================

    CROW_ROUTE(app, "/api/dashboard/priority")([](const crow::request& req) {
        stringstream out;
        out << "{\"algorithm\":{"
            << "\"formula\":\"score = categoria + complexidade + urgência + tempo de espera + porte do cliente + backbone + bônus de afetados (somatório)\","
            << "\"factors\":["
            << "{\"name\":\"Categoria do problema\",\"weight\":\"0 a 40\",\"desc\":\"Queda=40, Conectividade=25, Hardware=20, Wi-Fi=15, Instalação=10, Configuração=5\"},"
            << "{\"name\":\"Complexidade\",\"weight\":\"0 a 20\",\"desc\":\"(complexidade - 1) x 5 — maior complexidade, mais pontos\"},"
            << "{\"name\":\"Urgência relatada\",\"weight\":\"0 a 12\",\"desc\":\"(prioridade informada pelo cliente/gestor - 1) x 3\"},"
            << "{\"name\":\"Porte do cliente\",\"weight\":\"100 a 300\",\"desc\":\"Backbone=300, Empresa importante=250, Empresa simples/comércio=200, Residencial=100\"},"
            << "{\"name\":\"Tempo de espera\",\"weight\":\"0 a 15\",\"desc\":\"+1 ponto por hora desde a abertura (máx 15) — chamados antigos escalam sozinhos\"},"
            << "{\"name\":\"Clientes afetados (somatório)\",\"weight\":\"0 ou 250\",\"desc\":\"+250 pontos se a soma de clientes afetados na região for maior que 2\"}"
            << "],"
            << "\"mapping\":["
            << "{\"priority\":5,\"range\":\"450 ou mais\",\"label\":\"Crítica\"},"
            << "{\"priority\":4,\"range\":\"350 a 449\",\"label\":\"Alta\"},"
            << "{\"priority\":3,\"range\":\"250 a 349\",\"label\":\"Média\"},"
            << "{\"priority\":2,\"range\":\"150 a 249\",\"label\":\"Baixa\"},"
            << "{\"priority\":1,\"range\":\"0 a 149\",\"label\":\"Mínima\"}"
            << "]},"
            << "\"tickets\":"
            << g_db->query_to_json("SELECT t.id, t.title, t.status, t.priority, t.priority_score, t.priority_factors as factors, "
                                   "u.name as client_name FROM tickets t JOIN users u ON t.client_id = u.id "
                                   "WHERE t.status != 'COMPLETED' ORDER BY t.priority_score DESC, t.created_at DESC")
            << "}";
        crow::response res(out.str());
        res.set_header("Content-Type", "application/json");
        return res;
    });

    // ==================== TECHNICIANS ENDPOINTS ====================

    CROW_ROUTE(app, "/api/technicians")([](const crow::request& req) {
        string sql = "SELECT t.*, u.name, u.email, u.phone FROM technicians t "
                     "JOIN users u ON t.user_id = u.id ORDER BY t.skill_level DESC";
        string result = g_db->query_to_json(sql);
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    CROW_ROUTE(app, "/api/technicians/<int>")([](int id) {
        string sql = "SELECT t.*, u.name, u.email, u.phone FROM technicians t "
                     "JOIN users u ON t.user_id = u.id WHERE t.id = " + to_string(id);
        string result = g_db->query_to_json(sql);
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    CROW_ROUTE(app, "/api/technician/by-user/<int>")([](int user_id) {
        string sql = "SELECT t.*, u.name, u.email, u.phone FROM technicians t "
                     "JOIN users u ON t.user_id = u.id WHERE t.user_id = " + to_string(user_id);
        string result = g_db->query_to_json(sql);
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    CROW_ROUTE(app, "/api/technicians/<int>/status").methods("PUT"_method)([](const crow::request& req, int id) {
        auto params = parse_json(req.body);
        string status = params["status"];
        string sql = "UPDATE technicians SET current_status = '" + status +
                     "', last_update = datetime('now') WHERE id = " + to_string(id);
        g_db->execute(sql);

        return crow::response(200, "{\"success\":true}");
    });

    CROW_ROUTE(app, "/api/technicians/<int>/location").methods("PUT"_method)([](const crow::request& req, int id) {
        auto params = parse_json(req.body);
        string lat = params["latitude"];
        string lon = params["longitude"];
        string sql = "UPDATE technicians SET latitude = " + lat +
                     ", longitude = " + lon +
                     ", last_update = datetime('now') WHERE id = " + to_string(id);
        g_db->execute(sql);
        return crow::response(200, "{\"success\":true}");
    });

    // ==================== TICKETS ENDPOINTS ====================

    CROW_ROUTE(app, "/api/tickets")([](const crow::request& req) {
        string sql = "SELECT t.*, u.name as client_name, tech_u.name as technician_name "
                     "FROM tickets t "
                     "JOIN users u ON t.client_id = u.id "
                     "LEFT JOIN technicians tech ON t.assigned_technician_id = tech.id "
                     "LEFT JOIN users tech_u ON tech.user_id = tech_u.id "
                     "ORDER BY t.priority DESC, t.created_at DESC";
        string result = g_db->query_to_json(sql);
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    CROW_ROUTE(app, "/api/tickets").methods("POST"_method)([](const crow::request& req) {
        auto params = parse_json(req.body);
        string created_by = params["created_by"].empty() ? "NULL" : params["created_by"];
        // Priority is auto-calculated by the engine; the "priority" field sent is
        // treated as "urgência relatada" (reported urgency) — one of the public factors.
int urgency = params["priority"].empty() ? 3 : stoi(params["priority"]);
int complexity = params["complexity"].empty() ? 2 : stoi(params["complexity"]);
string problem_image = params["problem_image"];
if (problem_image.empty()) problem_image = "NULL";
else problem_image = "'" + problem_image + "'";
        string sql = "INSERT INTO tickets (client_id, title, description, category, priority, complexity, latitude, longitude, address, problem_image, created_by) VALUES ("
                     + params["client_id"] + ", '"
                     + params["title"] + "', '"
                     + params["description"] + "', '"
                     + params["category"] + "', "
                     + to_string(urgency) + ", "
                     + to_string(complexity) + ", "
                     + params["latitude"] + ", "
                     + params["longitude"] + ", '"
                     + params["address"] + "', " + problem_image + ", " + created_by + ")";
        long long id = g_db->execute_insert(sql);
        add_history(g_db, id, "PENDING", "Chamado criado em análise", params["title"]);
        PriorityCalculator::Factors f = PriorityCalculator::compute(g_db, id);
        g_db->execute("UPDATE tickets SET priority=" + to_string(f.priority) +
                      ", priority_score=" + to_string(f.total) +
                      ", priority_factors='" + PriorityCalculator::factors_to_json(f) +
                      "' WHERE id=" + to_string(id));
        return crow::response(201, "{\"id\":" + to_string(id) +
                                   ",\"success\":true"
                                   ",\"priority\":" + to_string(f.priority) +
                                   ",\"priority_score\":" + to_string(f.total) + "}");
    });

    CROW_ROUTE(app, "/api/tickets/<int>")([](int id) {
        string sql = "SELECT t.*, u.name as client_name, tech_u.name as technician_name "
                     "FROM tickets t "
                     "JOIN users u ON t.client_id = u.id "
                     "LEFT JOIN technicians tech ON t.assigned_technician_id = tech.id "
                     "LEFT JOIN users tech_u ON tech.user_id = tech_u.id "
                     "WHERE t.id = " + to_string(id);
        string result = g_db->query_to_json(sql);
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    CROW_ROUTE(app, "/api/tickets/<int>/assign").methods("PUT"_method)([](const crow::request& req, int id) {
        auto params = parse_json(req.body);
        string tech_id = params["technician_id"];

        g_db->execute("UPDATE tickets SET assigned_technician_id=" + tech_id +
                      ", status='ASSIGNED', assigned_at=datetime('now') WHERE id=" + to_string(id));

        g_db->execute("UPDATE technicians SET current_status='ASSIGNED', current_task_id=" +
                      to_string(id) + " WHERE id=" + tech_id);

        string tech_name = g_db->query_scalar(
            "SELECT u.name FROM technicians t JOIN users u ON t.user_id=u.id WHERE t.id=" + tech_id);
        add_history(g_db, id, "ASSIGNED", "Equipe mobilizada: " + tech_name, "Administrador");

        return crow::response(200, "{\"success\":true}");
    });

    CROW_ROUTE(app, "/api/tickets/<int>/status").methods("PUT"_method)([](const crow::request& req, int id) {
        auto params = parse_json(req.body);
        string status = params["status"];
        string sql = "UPDATE tickets SET status='" + status + "'";

        if (status == "IN_PROGRESS") {
            sql += ", started_at=datetime('now')";
        } else if (status == "COMPLETED") {
            // Conclusão exige evidência em foto
            string ev = g_db->query_scalar("SELECT COALESCE(evidence_image,'') FROM tickets WHERE id=" + to_string(id));
            if (ev.empty()) {
                return crow::response(422, "{\"error\":\"Envie uma foto de evidência antes de concluir o chamado\"}");
            }
            sql += ", completed_at=datetime('now')";
        }
        sql += " WHERE id=" + to_string(id);

        g_db->execute(sql);

        if (status == "IN_PROGRESS") {
            add_history(g_db, id, "IN_PROGRESS", "Operação iniciada", "Técnico");
        } else if (status == "COMPLETED") {
            // Finaliza também o estado do técnico responsável
            string tech_id = g_db->query_scalar("SELECT COALESCE(assigned_technician_id,0) FROM tickets WHERE id=" + to_string(id));
            if (!tech_id.empty() && tech_id != "0") {
                g_db->execute("UPDATE technicians SET current_task_id=NULL, current_status='IDLE', completed_tasks=completed_tasks+1 WHERE id=" + tech_id);
            }
            add_history(g_db, id, "COMPLETED", "Chamado finalizado com evidência (foto)", "Técnico");
        }

        return crow::response(200, "{\"success\":true}");
    });

    // ==================== UPLOAD DE IMAGEM ====================
    // kind = "evidence" (foto do técnico p/ concluir) ou "attachment" (foto do problema do cliente/gestor)

    CROW_ROUTE(app, "/api/tickets/<int>/image").methods("POST"_method)([](const crow::request& req, int id) {
        auto params = parse_json(req.body);
        string image = params["image"];
        string kind = params["kind"].empty() ? "attachment" : params["kind"];
        string user_name = params["user_name"].empty() ? "Sistema" : params["user_name"];

        if (image.empty()) {
            return crow::response(400, "{\"error\":\"Imagem vazia\"}");
        }

        string exists = g_db->query_scalar("SELECT id FROM tickets WHERE id=" + to_string(id));
        if (exists.empty()) {
            return crow::response(404, "{\"error\":\"Ticket não encontrado\"}");
        }

        if (kind == "evidence") {
            g_db->execute("UPDATE tickets SET evidence_image='" + image + "' WHERE id=" + to_string(id));
            add_history(g_db, id, "EVIDENCE", "Evidência (foto) enviada pelo técnico", user_name);
        } else {
            g_db->execute("UPDATE tickets SET problem_image='" + image + "' WHERE id=" + to_string(id));
            add_history(g_db, id, "ATTACHMENT", "Imagem do problema anexada", user_name);
        }
        return crow::response(200, "{\"success\":true}");
    });

    // Contatos de suporte (admin/gestor) para o app do técnico
    CROW_ROUTE(app, "/api/support")([](const crow::request& req) {
        string result = g_db->query_to_json(
            "SELECT username, name, role, COALESCE(phone,'') as phone, COALESCE(email,'') as email "
            "FROM users WHERE role IN ('admin','gestor') ORDER BY (role='gestor'), role");
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    // ==================== SMART ASSIGNMENT ====================

    CROW_ROUTE(app, "/api/tickets/<int>/suggest-technician")([](int ticket_id) {
        string sql = "SELECT complexity, latitude, longitude, category FROM tickets WHERE id=" + to_string(ticket_id);
        string ticket_json = g_db->query_to_json(sql);
        auto ticket = parse_json(ticket_json.substr(1, ticket_json.length() - 2));

        int complexity = stoi(ticket["complexity"]);
        double lat = stod(ticket["latitude"]);
        double lon = stod(ticket["longitude"]);
        string category = ticket["category"];

        auto matches = SkillMatcher::find_best_match(g_db, complexity, lat, lon, category);

        stringstream result;
        result << "[";
        for (size_t i = 0; i < matches.size() && i < 3; i++) {
            if (i > 0) result << ",";
            result << "{\"technician_id\":" << matches[i].technician_id
                   << ",\"score\":" << fixed << setprecision(1) << matches[i].score
                   << ",\"delay_minutes\":" << setprecision(1) << matches[i].delay_minutes
                   << ",\"reason\":\"" << matches[i].reason << "\"}";
        }
        result << "]";

        crow::response res(result.str());
        res.set_header("Content-Type", "application/json");
        return res;
    });

    CROW_ROUTE(app, "/api/traffic/events")([](const crow::request& req) {
        string result = TrafficSimulator::events_to_json(g_db);
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    // ==================== POP OUTAGE MONITORING ====================

    CROW_ROUTE(app, "/api/pop/status")([](const crow::request& req) {
        string result = POPMonitor::status_to_json(g_db);
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    // ==================== GESTOR (abertura de chamados) ====================

    CROW_ROUTE(app, "/api/clients")([](const crow::request& req) {
        string result = g_db->query_to_json(
            "SELECT id, name, phone, COALESCE(tier,'standard') as tier FROM users WHERE role='client' ORDER BY name");
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    CROW_ROUTE(app, "/api/gestor/tickets")([](const crow::request& req) {
        string result = g_db->query_to_json(
            "SELECT t.*, u.name as client_name, tech_u.name as technician_name "
            "FROM tickets t "
            "JOIN users u ON t.client_id = u.id "
            "LEFT JOIN technicians tech ON t.assigned_technician_id = tech.id "
            "LEFT JOIN users tech_u ON tech.user_id = tech_u.id "
            "WHERE t.created_by = (SELECT id FROM users WHERE username='gestor') "
            "ORDER BY t.created_at DESC");
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    // ==================== FIBER NETWORK ====================

    CROW_ROUTE(app, "/api/fiber/nodes")([](const crow::request& req) {
        string result = g_db->query_to_json("SELECT * FROM fiber_nodes");
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    CROW_ROUTE(app, "/api/fiber/triangulate").methods("POST"_method)([](const crow::request& req) {
        auto params = parse_json(req.body);
        double lat = stod(params["latitude"]);
        double lon = stod(params["longitude"]);
        string result = FiberTriangulator::triangulate_fault(lat, lon);
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    // ==================== ROUTES / LOGISTICS ====================

    CROW_ROUTE(app, "/api/routes/optimize").methods("POST"_method)([](const crow::request& req) {
        auto params = parse_json(req.body);
        string tech_id = params["technician_id"];
        string ticket_ids_str = params["ticket_ids"];

        vector<int> ticket_ids;
        stringstream ss(ticket_ids_str);
        string id;
        while (getline(ss, id, ',')) {
            ticket_ids.push_back(stoi(id));
        }

        string result = RouteOptimizer::optimize_route(g_db, stoi(tech_id), ticket_ids);
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    // ==================== CLIENT SPECIFIC ====================

    CROW_ROUTE(app, "/api/client/<int>/tickets")([](int client_id) {
        string sql = "SELECT t.*, tech_u.name as technician_name FROM tickets t "
                     "LEFT JOIN technicians tech ON t.assigned_technician_id = tech.id "
                     "LEFT JOIN users tech_u ON tech.user_id = tech_u.id "
                     "WHERE t.client_id = " + to_string(client_id) +
                     " ORDER BY t.created_at DESC";
        string result = g_db->query_to_json(sql);
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    CROW_ROUTE(app, "/api/tickets/<int>/history")([](int ticket_id) {
        string sql = "SELECT * FROM ticket_history WHERE ticket_id = " + to_string(ticket_id) +
                     " ORDER BY created_at ASC";
        string result = g_db->query_to_json(sql);
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    // ==================== TECHNICIAN SPECIFIC ====================

    CROW_ROUTE(app, "/api/technician/<int>/tasks")([](int tech_id) {
        string sql = "SELECT t.*, u.name as client_name FROM tickets t "
                     "JOIN users u ON t.client_id = u.id "
                     "WHERE t.assigned_technician_id = " + to_string(tech_id) +
                     " AND t.status IN ('ASSIGNED', 'IN_PROGRESS') "
                     "ORDER BY t.priority DESC";
        string result = g_db->query_to_json(sql);
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    CROW_ROUTE(app, "/api/technician/<int>/eta").methods("POST"_method)([](const crow::request& req, int tech_id) {
        auto params = parse_json(req.body);
        string eta_minutes = params["eta_minutes"];
        string ticket_id = params["ticket_id"];

        g_db->execute("UPDATE tickets SET estimated_duration=" + eta_minutes +
                      " WHERE id=" + ticket_id);

        return crow::response(200, "{\"success\":true}");
    });

    // ==================== NOTIFICATIONS ====================

    CROW_ROUTE(app, "/api/notifications/<int>")([](int user_id) {
        string sql = "SELECT * FROM notifications WHERE user_id = " + to_string(user_id) +
                     " AND read = 0 ORDER BY created_at DESC";
        string result = g_db->query_to_json(sql);
        crow::response res(result);
        res.set_header("Content-Type", "application/json");
        return res;
    });

    CROW_ROUTE(app, "/api/notifications/<int>/read").methods("PUT"_method)([](int user_id) {
        g_db->execute("UPDATE notifications SET read = 1 WHERE user_id = " + to_string(user_id));
        return crow::response(200, "{\"success\":true}");
    });

    // ==================== CORS & OPTIONS ====================

    CROW_ROUTE(app, "/api/<path>").methods("OPTIONS"_method)([](string path) {
        crow::response res(204);
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    // ==================== START SERVER ====================

    cout << "==============================================" << endl;
    cout << "   AYKO Operations Platform - Backend C++    " << endl;
    cout << "   Hackathon Inovahack 2026 - Equipe Kaino   " << endl;
    cout << "==============================================" << endl;
    cout << "   Server:     http://localhost:8080          " << endl;
    cout << "   Admin:      http://localhost:8080/admin    " << endl;
    cout << "   Cliente:    http://localhost:8080/client   " << endl;
    cout << "   Técnico:    http://localhost:8080/tecnico  " << endl;
    cout << "   Gestor:     http://localhost:8080/gestor   " << endl;
    cout << "==============================================" << endl;

    app.port(8080).multithreaded().run();

    delete g_db;
    return 0;
}
