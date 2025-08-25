#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <fstream>
#define RESET "\033[0m"
#define CYAN "\033[36m"

#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

using namespace std;
using namespace sql;
using namespace sql::mysql; 

class Person
{
protected:
    string id;
    string name;
    string email;

public:
    Person(const string& id, const string& name, const string& email)
        : id(id), name(name), email(email)
    {
    }
    virtual void menu() = 0;
    virtual string getRole() const = 0;
    virtual ~Person() {}
    string getId() const { return id; }
    string getName() const { return name; }
    string getEmail() const { return email; }
};

class Database
{
    MySQL_Driver* driver;
    unique_ptr<Connection> con;

public:
    Database(const string& host, const string& user, const string& pass, const string& db)
    {
        driver = get_mysql_driver_instance();
        con.reset(driver->connect(host, user, pass));
        con->setSchema(db);
    }
    ~Database()
    {
        if (con)
            con->close();
    }

    bool studentExists(const string& studentId)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement("SELECT COUNT(*) FROM students WHERE student_id = ?"));
        pstmt->setString(1, studentId);
        auto res = unique_ptr<ResultSet>(pstmt->executeQuery());
        return (res->next() && res->getInt(1) > 0);
    }
    int getStudentSemester(const string& studentId)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement("SELECT semester FROM students WHERE student_id = ?"));
        pstmt->setString(1, studentId);
        auto res = unique_ptr<ResultSet>(pstmt->executeQuery());
        return (res->next() ? res->getInt("semester") : -1);
    }
    string getStudentDegree(const string& studentId)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement("SELECT degree FROM students WHERE student_id = ?"));
        pstmt->setString(1, studentId);
        auto res = unique_ptr<ResultSet>(pstmt->executeQuery());
        return (res->next() ? res->getString("degree") : "");
    }

    struct ScheduledCourse
    {
        int schedule_id;
        string course_code, course_name, department;
        int semester, faculty_id, timeslot_id;
        string faculty_name, day, start_time, end_time;
        string room_id, room_number, building;
    };
    vector<ScheduledCourse> getAvailableScheduledCourses(int semester, const string& degree)
    {
        vector<ScheduledCourse> result;
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "SELECT cs.schedule_id, c.course_code, c.course_name, c.department, c.semester, "
            "f.faculty_id, CONCAT(f.first_name,' ',f.last_name) AS faculty_name, "
            "t.timeslot_id, t.day_of_week, t.start_time, t.end_time, "
            "cl.room_id, cl.room_number, cl.building "
            "FROM course_schedule cs "
            "JOIN courses c ON cs.course_code = c.course_code "
            "JOIN faculty f ON cs.faculty_id = f.faculty_id "
            "JOIN timeslots t ON cs.timeslot_id = t.timeslot_id "
            "JOIN classrooms cl ON cs.room_id = cl.room_id "
            "WHERE c.semester = ? AND c.department = ?"));
        pstmt->setInt(1, semester);
        pstmt->setString(2, degree);
        auto res = unique_ptr<ResultSet>(pstmt->executeQuery());
        while (res->next())
        {
            result.push_back({ res->getInt("schedule_id"),
                              res->getString("course_code"),
                              res->getString("course_name"),
                              res->getString("department"),
                              res->getInt("semester"),
                              res->getInt("faculty_id"),
                              res->getInt("timeslot_id"),
                              res->getString("faculty_name"),
                              res->getString("day_of_week"),
                              res->getString("start_time"),
                              res->getString("end_time"),
                              res->getString("room_id"),
                              res->getString("room_number"),
                              res->getString("building") });
        }
        return result;
    }
    bool isAlreadyEnrolled(const string& studentId, int schedule_id)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "SELECT COUNT(*) FROM enrollments WHERE student_id = ? AND schedule_id = ?"));
        pstmt->setString(1, studentId);
        pstmt->setInt(2, schedule_id);
        auto res = unique_ptr<ResultSet>(pstmt->executeQuery());
        return (res->next() && res->getInt(1) > 0);
    }
    bool hasClash(const string& studentId, int timeslot_id)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "SELECT COUNT(*) FROM enrollments e "
            "JOIN course_schedule cs ON e.schedule_id = cs.schedule_id "
            "WHERE e.student_id = ? AND cs.timeslot_id = ?"));
        pstmt->setString(1, studentId);
        pstmt->setInt(2, timeslot_id);
        auto res = unique_ptr<ResultSet>(pstmt->executeQuery());
        return (res->next() && res->getInt(1) > 0);
    }
    bool addEnrollment(const string& studentId, int schedule_id)
    {
        auto pstmt_code = unique_ptr<PreparedStatement>(con->prepareStatement(
            "SELECT course_code FROM course_schedule WHERE schedule_id = ?"));
        pstmt_code->setInt(1, schedule_id);
        auto res_code = unique_ptr<ResultSet>(pstmt_code->executeQuery());
        string course_code;
        if (res_code->next())
            course_code = res_code->getString(1);

        auto pstmt_max = unique_ptr<PreparedStatement>(con->prepareStatement(
            "SELECT max_students FROM courses WHERE course_code = ?"));
        pstmt_max->setString(1, course_code);
        auto res_max = unique_ptr<ResultSet>(pstmt_max->executeQuery());
        int max_students = 0;
        if (res_max->next())
            max_students = res_max->getInt(1);

        auto pstmt_count = unique_ptr<PreparedStatement>(con->prepareStatement(
            "SELECT COUNT(*) FROM enrollments WHERE schedule_id = ?"));
        pstmt_count->setInt(1, schedule_id);
        auto res_count = unique_ptr<ResultSet>(pstmt_count->executeQuery());
        int enrolled = 0;
        if (res_count->next())
            enrolled = res_count->getInt(1);

        if (enrolled >= max_students)
            return false;

        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "INSERT INTO enrollments (student_id, schedule_id) VALUES (?, ?)"));
        pstmt->setString(1, studentId);
        pstmt->setInt(2, schedule_id);
        pstmt->execute();
        return true;
    }
    bool dropEnrollment(const string& studentId, int schedule_id)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "DELETE FROM enrollments WHERE student_id = ? AND schedule_id = ?"));
        pstmt->setString(1, studentId);
        pstmt->setInt(2, schedule_id);
        return pstmt->executeUpdate() > 0;
    }
    vector<ScheduledCourse> getEnrolledCourses(const string& studentId)
    {
        vector<ScheduledCourse> result;
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "SELECT cs.schedule_id, c.course_code, c.course_name, c.department, c.semester, "
            "f.faculty_id, CONCAT(f.first_name,' ',f.last_name) AS faculty_name, "
            "t.timeslot_id, t.day_of_week, t.start_time, t.end_time, "
            "cl.room_id, cl.room_number, cl.building "
            "FROM enrollments e "
            "JOIN course_schedule cs ON e.schedule_id = cs.schedule_id "
            "JOIN courses c ON cs.course_code = c.course_code "
            "JOIN faculty f ON cs.faculty_id = f.faculty_id "
            "JOIN timeslots t ON cs.timeslot_id = t.timeslot_id "
            "JOIN classrooms cl ON cs.room_id = cl.room_id "
            "WHERE e.student_id = ?"));
        pstmt->setString(1, studentId);
        auto res = unique_ptr<ResultSet>(pstmt->executeQuery());
        while (res->next())
        {
            result.push_back({ res->getInt("schedule_id"),
                              res->getString("course_code"),
                              res->getString("course_name"),
                              res->getString("department"),
                              res->getInt("semester"),
                              res->getInt("faculty_id"),
                              res->getInt("timeslot_id"),
                              res->getString("faculty_name"),
                              res->getString("day_of_week"),
                              res->getString("start_time"),
                              res->getString("end_time"),
                              res->getString("room_id"),
                              res->getString("room_number"),
                              res->getString("building") });
        }
        return result;
    }
    typedef ScheduledCourse TimetableEntry;
    vector<TimetableEntry> getStudentTimetable(const string& studentId)
    {
        return getEnrolledCourses(studentId);
    }


    int getNextFacultyId()
    {
        auto stmt = unique_ptr<Statement>(con->createStatement());
        auto res = unique_ptr<ResultSet>(stmt->executeQuery("SELECT MAX(faculty_id) FROM faculty"));
        int nextId = 1;
        if (res->next())
        {
            nextId = res->getInt(1) + 1;
            if (res->isNull(1))
                nextId = 1;
        }
        return nextId;
    }

    void addStudent(const string& id, const string& fname, const string& lname, const string& email, const string& degree, int semester)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "INSERT INTO students (student_id, first_name, last_name, email, degree, semester) VALUES (?, ?, ?, ?, ?, ?)"));
        pstmt->setString(1, id);
        pstmt->setString(2, fname);
        pstmt->setString(3, lname);
        pstmt->setString(4, email);
        pstmt->setString(5, degree);
        pstmt->setInt(6, semester);
        pstmt->execute();
    }
    void removeStudent(const string& id)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "DELETE FROM students WHERE student_id = ?"));
        pstmt->setString(1, id);
        pstmt->execute();
    }
    void addFaculty(int faculty_id, const string& fname, const string& lname, const string& email, const string& degree, const string& qualification, const string& expertise_sub, const string& designation)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "INSERT INTO faculty (faculty_id, first_name, last_name, email, degree, qualification, expertise_sub, designation) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
        pstmt->setInt(1, faculty_id);
        pstmt->setString(2, fname);
        pstmt->setString(3, lname);
        pstmt->setString(4, email);
        pstmt->setString(5, degree);
        pstmt->setString(6, qualification);
        pstmt->setString(7, expertise_sub);
        pstmt->setString(8, designation);
        pstmt->execute();
    }
    void removeFaculty(int faculty_id)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "DELETE FROM faculty WHERE faculty_id = ?"));
        pstmt->setInt(1, faculty_id);
        pstmt->execute();
    }
    void addCourse(const string& code, const string& name, int credits, int sem, const string& dept, int max, const string& prereq)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "INSERT INTO courses (course_code, course_name, credits, semester, department, max_students, prerequisites) VALUES (?, ?, ?, ?, ?, ?, ?)"));
        pstmt->setString(1, code);
        pstmt->setString(2, name);
        pstmt->setInt(3, credits);
        pstmt->setInt(4, sem);
        pstmt->setString(5, dept);
        pstmt->setInt(6, max);
        pstmt->setString(7, prereq);
        pstmt->execute();
    }
    void removeCourse(const string& code)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "DELETE FROM courses WHERE course_code = ?"));
        pstmt->setString(1, code);
        pstmt->execute();
    }
    void addClassroom(const string& id, const string& building, const string& number, int capacity, const string& room_type)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "INSERT INTO classrooms (room_id, building, room_number, capacity, room_type) VALUES (?, ?, ?, ?, ?)"));
        pstmt->setString(1, id);
        pstmt->setString(2, building);
        pstmt->setString(3, number);
        pstmt->setInt(4, capacity);
        pstmt->setString(5, room_type);
        pstmt->execute();
    }
    void removeClassroom(const string& id)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "DELETE FROM classrooms WHERE room_id = ?"));
        pstmt->setString(1, id);
        pstmt->execute();
    }
    void addTimeslot(const string& day, const string& start, const string& end)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "INSERT INTO timeslots (day_of_week, start_time, end_time) VALUES (?, ?, ?)"));
        pstmt->setString(1, day);
        pstmt->setString(2, start);
        pstmt->setString(3, end);
        pstmt->execute();
    }
    void removeTimeslot(int timeslot_id)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "DELETE FROM timeslots WHERE timeslot_id = ?"));
        pstmt->setInt(1, timeslot_id);
        pstmt->execute();
    }
    vector<pair<string, string>> getUnscheduledCourses()
    {
        vector<pair<string, string>> resvec;
        auto stmt = unique_ptr<Statement>(con->createStatement());
        auto res = unique_ptr<ResultSet>(stmt->executeQuery(
            "SELECT course_code, course_name FROM courses WHERE course_code NOT IN (SELECT course_code FROM course_schedule)"));
        while (res->next())
            resvec.emplace_back(res->getString(1), res->getString(2));
        return resvec;
    }
    vector<pair<int, string>> getAllTimeslots()
    {
        vector<pair<int, string>> resvec;
        auto stmt = unique_ptr<Statement>(con->createStatement());
        auto res = unique_ptr<ResultSet>(stmt->executeQuery("SELECT timeslot_id, CONCAT(day_of_week, ' ', start_time, '-', end_time) FROM timeslots"));
        while (res->next())
            resvec.emplace_back(res->getInt(1), res->getString(2));
        return resvec;
    }
    vector<pair<string, string>> getAvailableRooms(int timeslot_id)
    {
        vector<pair<string, string>> resvec;
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "SELECT room_id, CONCAT(room_number, ' ', building) FROM classrooms "
            "WHERE room_id NOT IN (SELECT room_id FROM course_schedule WHERE timeslot_id = ?)"));
        pstmt->setInt(1, timeslot_id);
        auto res = unique_ptr<ResultSet>(pstmt->executeQuery());
        while (res->next())
            resvec.emplace_back(res->getString(1), res->getString(2));
        return resvec;
    }
    vector<pair<int, string>> getAvailableFaculty(int timeslot_id)
    {
        vector<pair<int, string>> resvec;
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "SELECT faculty_id, CONCAT(first_name, ' ', last_name) FROM faculty "
            "WHERE faculty_id NOT IN (SELECT faculty_id FROM course_schedule WHERE timeslot_id = ?)"));
        pstmt->setInt(1, timeslot_id);
        auto res = unique_ptr<ResultSet>(pstmt->executeQuery());
        while (res->next())
            resvec.emplace_back(res->getInt(1), res->getString(2));
        return resvec;
    }
    void addCourseSchedule(const string& course_code, int faculty_id, int timeslot_id, const string& room_id)
    {
        auto pstmt = unique_ptr<PreparedStatement>(con->prepareStatement(
            "INSERT INTO course_schedule (course_code, faculty_id, timeslot_id, room_id) VALUES (?, ?, ?, ?)"));
        pstmt->setString(1, course_code);
        pstmt->setInt(2, faculty_id);
        pstmt->setInt(3, timeslot_id);
        pstmt->setString(4, room_id);
        pstmt->execute();
    }
    struct ScheduledAssignment
    {
        int schedule_id;
        string course_code, course_name, faculty_name, room, timeslot;
    };
    vector<ScheduledAssignment> getAllCourseSchedules()
    {
        vector<ScheduledAssignment> result;
        auto stmt = unique_ptr<Statement>(con->createStatement());
        auto res = unique_ptr<ResultSet>(stmt->executeQuery(
            "SELECT cs.schedule_id, cs.course_code, c.course_name, CONCAT(f.first_name, ' ', f.last_name) AS faculty, "
            "CONCAT(cl.room_number, ' ', cl.building) AS room, CONCAT(t.day_of_week, ' ', t.start_time, '-', t.end_time) AS timeslot "
            "FROM course_schedule cs "
            "JOIN courses c ON cs.course_code = c.course_code "
            "JOIN faculty f ON cs.faculty_id = f.faculty_id "
            "JOIN timeslots t ON cs.timeslot_id = t.timeslot_id "
            "JOIN classrooms cl ON cs.room_id = cl.room_id"));
        while (res->next())
        {
            result.push_back({ res->getInt("schedule_id"),
                              res->getString("course_code"),
                              res->getString("course_name"),
                              res->getString("faculty"),
                              res->getString("room"),
                              res->getString("timeslot") });
        }
        return result;
    }
    void removeCourseSchedule(int schedule_id)
    {
        auto pstmt1 = unique_ptr<PreparedStatement>(con->prepareStatement(
            "DELETE FROM enrollments WHERE schedule_id = ?"));
        pstmt1->setInt(1, schedule_id);
        pstmt1->execute();
        auto pstmt2 = unique_ptr<PreparedStatement>(con->prepareStatement(
            "DELETE FROM course_schedule WHERE schedule_id = ?"));
        pstmt2->setInt(1, schedule_id);
        pstmt2->execute();
    }
    bool isAdminPasswordCorrect(const string& password)
    {
        return password == "admin123";
    }
};

class Student : public Person
{
    Database& db;

public:
    Student(Database& db, const string& id, const string& name, const string& email)
        : Person(id, name, email), db(db)
    {
    }
    void menu() override
    {
        int choice;
        do
        {
            cout << CYAN << "\n--- Student Menu ---\n"
                << RESET;
            cout << "1. Add Course\n";
            cout << "2. Drop Course\n";
            cout << "3. View Timetable\n";
            cout << "4. View Teachers\n";
            cout << "5. View Classroom Details\n";
            cout << "6. Export Timetable\n";
            cout << "0. Logout\n";
            cout << "Choice: ";
            cin >> choice;
            switch (choice)
            {
            case 1:
                addCourse();
                break;
            case 2:
                dropCourse();
                break;
            case 3:
                viewTimetable();
                break;
            case 4:
                viewTeachers();
                break;
            case 5:
                viewClassroomDetails();
                break;
            case 6:
                exportTimetable();
                break;
            case 0:
                cout << "Logging out...\n";
                break;
            default:
                cout << "Invalid choice.\n";
            }
        } while (choice != 0);
    }
    string getRole() const override { return "Student"; }
    void addCourse()
    {
        int sem = db.getStudentSemester(id);
        string deg = db.getStudentDegree(id);
        auto courses = db.getAvailableScheduledCourses(sem, deg);
        if (courses.empty())
        {
            cout << "No scheduled courses for your degree/semester.\n";
            return;
        }
        cout << "Available scheduled courses:\n";
        for (size_t i = 0; i < courses.size(); ++i)
            cout << i + 1 << ". " << courses[i].course_code << " - " << courses[i].course_name
            << " | " << courses[i].faculty_name << " | " << courses[i].day
            << " " << courses[i].start_time << "-" << courses[i].end_time
            << " | " << courses[i].room_number << " " << courses[i].building << endl;
        cout << "Enter course number to add: ";
        int cidx;
        cin >> cidx;
        if (cidx < 1 || cidx >(int)courses.size())
        {
            cout << "Invalid.\n";
            return;
        }
        auto& sc = courses[cidx - 1];
        if (db.isAlreadyEnrolled(id, sc.schedule_id))
        {
            cout << "Already enrolled in this course.\n";
            return;
        }
        if (db.hasClash(id, sc.timeslot_id))
        {
            cout << "Course timeslot clashes with your existing courses.\n";
            return;
        }
        if (db.addEnrollment(id, sc.schedule_id))
            cout << "Enrolled successfully.\n";
        else
            cout << "Course full or error occurred.\n";
    }
    void dropCourse()
    {
        auto enrolled = db.getEnrolledCourses(id);
        if (enrolled.empty())
        {
            cout << "No enrolled courses.\n";
            return;
        }
        for (size_t i = 0; i < enrolled.size(); ++i)
            cout << i + 1 << ". " << enrolled[i].course_code << " - " << enrolled[i].course_name << " | "
            << enrolled[i].faculty_name << " | " << enrolled[i].day << " " << enrolled[i].start_time << "-" << enrolled[i].end_time << endl;
        cout << "Enter course number to drop: ";
        int cidx;
        cin >> cidx;
        if (cidx < 1 || cidx >(int)enrolled.size())
        {
            cout << "Invalid.\n";
            return;
        }
        int schedule_id = enrolled[cidx - 1].schedule_id;
        if (db.dropEnrollment(id, schedule_id))
            cout << "Dropped successfully.\n";
        else
            cout << "Error or not enrolled.\n";
    }
    void viewTimetable()
    {
        auto tt = db.getStudentTimetable(id);
        if (tt.empty())
        {
            cout << "No enrolled courses.\n";
            return;
        }
        cout << CYAN << left << setw(10) << "Course" << setw(32) << "Name" << setw(10) << "Day"
            << setw(12) << "Start" << setw(12) << "End" << setw(10) << "Room"
            << setw(10) << "Bldg" << setw(20) << "Teacher" << RESET << endl;
        for (size_t i = 0; i < tt.size(); ++i)
        {
            const auto& t = tt[i];
            cout << setw(10) << t.course_code << setw(32) << t.course_name << setw(10) << t.day
                << setw(12) << t.start_time << setw(12) << t.end_time << setw(10) << t.room_number
                << setw(10) << t.building << setw(20) << t.faculty_name << endl;
        }
    }
    void viewTeachers()
    {
        auto tt = db.getStudentTimetable(id);
        cout << "Your Teachers:\n";
        for (size_t i = 0; i < tt.size(); ++i)
        {
            bool alreadyShown = false;
            for (size_t j = 0; j < i; ++j)
            {
                if (tt[j].faculty_name == tt[i].faculty_name)
                {
                    alreadyShown = true;
                    break;
                }
            }
            if (!alreadyShown)
            {
                cout << "- " << tt[i].faculty_name << endl;
            }
        }
    }
    void viewClassroomDetails()
    {
        auto tt = db.getStudentTimetable(id);
        cout << "Your Classrooms:\n";
        for (size_t i = 0; i < tt.size(); ++i)
        {
            bool alreadyShown = false;
            for (size_t j = 0; j < i; ++j)
            {
                if (tt[j].room_number == tt[i].room_number && tt[j].building == tt[i].building)
                {
                    alreadyShown = true;
                    break;
                }
            }
            if (!alreadyShown)
            {
                cout << "- Room " << tt[i].room_number << " in " << tt[i].building << endl;
            }
        }
    }
    void exportTimetable()
    {
        auto tt = db.getStudentTimetable(id);
        ofstream out(id + "_timetable.csv");
        out << "Course,Name,Day,Start,End,Room,Bldg,Teacher\n";
        for (size_t i = 0; i < tt.size(); ++i)
        {
            const auto& t = tt[i];
            out << t.course_code << "," << t.course_name << "," << t.day << "," << t.start_time << ","
                << t.end_time << "," << t.room_number << "," << t.building << "," << t.faculty_name << "\n";
        }
        out.close();
        cout << "Timetable exported to " << id << "_timetable.csv\n";
    }
};

class Admin : public Person
{
    Database& db;

public:
    Admin(Database& db, const string& id, const string& name, const string& email)
        : Person(id, name, email), db(db)
    {
    }
    void menu() override
    {
        int choice;
        do
        {
            cout << CYAN << "\n--- Admin Menu ---\n"
                << RESET;
            cout << "1. Add Student\n";
            cout << "2. Remove Student\n";
            cout << "3. Add Faculty\n";
            cout << "4. Remove Faculty\n";
            cout << "5. Add Course\n";
            cout << "6. Remove Course\n";
            cout << "7. Add Classroom\n";
            cout << "8. Remove Classroom\n";
            cout << "9. Add Timeslot\n";
            cout << "10. Remove Timeslot\n";
            cout << "11. Assign Course/Teacher/Timeslot/Classroom\n";
            cout << "12. Remove Course Assignment\n";
            cout << "0. Logout\n";
            cout << "Choice: ";
            cin >> choice;
            switch (choice)
            {
            case 1:
                addStudent();
                break;
            case 2:
                removeStudent();
                break;
            case 3:
                addFaculty();
                break;
            case 4:
                removeFaculty();
                break;
            case 5:
                addCourse();
                break;
            case 6:
                removeCourse();
                break;
            case 7:
                addClassroom();
                break;
            case 8:
                removeClassroom();
                break;
            case 9:
                addTimeslot();
                break;
            case 10:
                removeTimeslot();
                break;
            case 11:
                assignCourseSchedule();
                break;
            case 12:
                removeCourseAssignment();
                break;
            case 0:
                cout << "Logging out...\n";
                break;
            default:
                cout << "Invalid choice.\n";
            }
        } while (choice != 0);
    }
    string getRole() const override { return "Admin"; }

    void addStudent()
    {
        string id, fname, lname, email, degree;
        int semester;
        cout << "Student ID: ";
        cin >> id;
        cin.ignore();
        cout << "First name: ";
        getline(cin, fname);
        cout << "Last name: ";
        getline(cin, lname);
        cout << "Email: ";
        cin >> email;
        cin.ignore();
        cout << "Degree: ";
        getline(cin, degree);
        cout << "Semester: ";
        cin >> semester;
        db.addStudent(id, fname, lname, email, degree, semester);
        cout << "Student added.\n";
    }
    void removeStudent()
    {
        string id;
        cout << "Student ID to remove: ";
        cin >> id;
        db.removeStudent(id);
        cout << "Student removed.\n";
    }
    void addFaculty()
    {
        int faculty_id;
        string fname, lname, email, degree, qualification, expertise_sub, designation;
        cout << "Faculty ID: ";
        cin >> faculty_id;
        cin.ignore();
        cout << "First name: ";
        getline(cin, fname);
        cout << "Last name: ";
        getline(cin, lname);
        cout << "Email: ";
        cin >> email;
        cin.ignore();
        cout << "Degree: ";
        getline(cin, degree);
        cout << "Qualification: ";
        cin >> qualification;
        cin.ignore();
        cout << "Expertise subject: ";
        getline(cin, expertise_sub);
        cout << "Designation: ";
        getline(cin, designation);
        db.addFaculty(faculty_id, fname, lname, email, degree, qualification, expertise_sub, designation);
        cout << "Faculty added.\n";
    }
    void removeFaculty()
    {
        int id;
        cout << "Faculty ID to remove: ";
        cin >> id;
        db.removeFaculty(id);
        cout << "Faculty removed.\n";
    }
    void addCourse()
    {
        string code, name, dept, prereq;
        int sem, max, credits;
        cout << "Course code: ";
        cin >> code;
        cin.ignore();
        cout << "Course name: ";
        getline(cin, name);
        cout << "Credits: ";
        cin >> credits;
        cout << "Semester: ";
        cin >> sem;
        cin.ignore();
        cout << "Department: ";
        getline(cin, dept);
        cout << "Max students: ";
        cin >> max;
        cin.ignore();
        cout << "Prerequisites: ";
        getline(cin, prereq);
        db.addCourse(code, name, credits, sem, dept, max, prereq);
        cout << "Course added.\n";
    }
    void removeCourse()
    {
        string code;
        cout << "Course code to remove: ";
        cin >> code;
        db.removeCourse(code);
        cout << "Course removed.\n";
    }
    void addClassroom()
    {
        string id, number, building, room_type;
        int capacity;
        cout << "Room ID: ";
        cin >> id;
        cout << "Room number: ";
        cin >> number;
        cout << "Building: ";
        cin >> building;
        cout << "Capacity: ";
        cin >> capacity;
        cout << "Room type: ";
        cin >> room_type;
        db.addClassroom(id, building, number, capacity, room_type);
        cout << "Classroom added.\n";
    }
    void removeClassroom()
    {
        string id;
        cout << "Room ID to remove: ";
        cin >> id;
        db.removeClassroom(id);
        cout << "Classroom removed.\n";
    }
    void addTimeslot()
    {
        string day, start, end;
        cout << "Day of week: ";
        cin >> day;
        cout << "Start time (HH:MM:SS): ";
        cin >> start;
        cout << "End time (HH:MM:SS): ";
        cin >> end;
        db.addTimeslot(day, start, end);
        cout << "Timeslot added.\n";
    }
    void removeTimeslot()
    {
        int id;
        cout << "Timeslot ID to remove: ";
        cin >> id;
        db.removeTimeslot(id);
        cout << "Timeslot removed.\n";
    }
    void assignCourseSchedule()
    {
        auto courses = db.getUnscheduledCourses();
        if (courses.empty())
        {
            cout << "All courses are already assigned. Remove an assignment to reassign.\n";
            return;
        }
        auto timeslots = db.getAllTimeslots();
        int c, f, t, r;
        cout << "Courses:\n";
        for (size_t i = 0; i < courses.size(); ++i)
            cout << i + 1 << ". " << courses[i].first << " - " << courses[i].second << endl;
        cout << "Select course: ";
        cin >> c;
        cout << "Timeslots:\n";
        for (size_t i = 0; i < timeslots.size(); ++i)
            cout << timeslots[i].first << " - " << timeslots[i].second << endl;
        cout << "Select timeslot: ";
        cin >> t;
        if (c < 1 || c >(int)courses.size() || t < 1 || t >(int)timeslots.size())
        {
            cout << "Invalid selection.\n";
            return;
        }
        auto availableFaculty = db.getAvailableFaculty(timeslots[t - 1].first);
        if (availableFaculty.empty())
        {
            cout << "No available faculty for this timeslot.\n";
            return;
        }
        cout << "Faculty:\n";
        for (size_t i = 0; i < availableFaculty.size(); ++i)
            cout << i + 1 << ". " << availableFaculty[i].first << " - " << availableFaculty[i].second << endl;
        cout << "Select faculty: ";
        cin >> f;
        if (f < 1 || f >(int)availableFaculty.size())
        {
            cout << "Invalid selection.\n";
            return;
        }
        auto rooms = db.getAvailableRooms(timeslots[t - 1].first);
        if (rooms.empty())
        {
            cout << "No available rooms for this timeslot.\n";
            return;
        }
        cout << "Rooms:\n";
        for (size_t i = 0; i < rooms.size(); ++i)
            cout << i + 1 << ". " << rooms[i].first << " - " << rooms[i].second << endl;
        cout << "Select room: ";
        cin >> r;
        if (r < 1 || r >(int)rooms.size())
        {
            cout << "Invalid selection.\n";
            return;
        }
        db.addCourseSchedule(
            courses[c - 1].first,
            availableFaculty[f - 1].first,
            timeslots[t - 1].first,
            rooms[r - 1].first);
        cout << "Assignment completed.\n";
    }
    void removeCourseAssignment()
    {
        auto assignments = db.getAllCourseSchedules();
        if (assignments.empty())
        {
            cout << "No assigned courses.\n";
            return;
        }
        for (size_t i = 0; i < assignments.size(); ++i)
            cout << i + 1 << ". " << assignments[i].course_code << " - " << assignments[i].course_name
            << " | " << assignments[i].faculty_name << " | " << assignments[i].room << " | " << assignments[i].timeslot << endl;
        cout << "Select assignment to remove: ";
        int idx;
        cin >> idx;
        if (idx < 1 || idx >(int)assignments.size())
        {
            cout << "Invalid selection.\n";
            return;
        }
        db.removeCourseSchedule(assignments[idx - 1].schedule_id);
        cout << "Assignment removed.\n";
    }
};

int main()
{
    string host = "tcp://127.0.0.1:3306";
    string user = "root";
    string pass = "Sufian312";
    string dbname = "project_db";
    try
    {
        Database db(host, user, pass, dbname);
        int choice;
        do
        {
            cout << CYAN << "\n--- SCIT Management System ---" << RESET << endl;
            cout << "1. Student Login\n";
            cout << "2. Admin Login\n";
            cout << "0. Exit\n";
            cout << "Choice: ";
            cin >> choice;
            if (choice == 1)
            {
                string studentId;
                cout << "Enter Student ID: ";
                cin >> studentId;
                if (db.studentExists(studentId))
                {
                    Student stu(db, studentId, "StudentName", "student@email.com");
                    stu.menu();
                }
                else
                {
                    cout << "Student ID not found.\n";
                }
            }
            else if (choice == 2)
            {
                string password;
                cout << "Enter Admin Password: ";
                cin >> password;
                if (db.isAdminPasswordCorrect(password))
                {
                    Admin admin(db, "admin", "Admin", "admin@email.com");
                    admin.menu();
                }
                else
                {
                    cout << "Invalid password.\n";
                }
            }
            else if (choice == 0)
            {
                cout << "Exiting...\n";
            }
            else
            {
                cout << "Invalid choice.\n";
            }
        } while (choice != 0);
    }
    catch (SQLException& ex)
    {
        cerr << "Database error: " << ex.what() << endl;
    }
    catch (exception& ex)
    {
        cerr << "Error: " << ex.what() << endl;
    }
    return 0;
}