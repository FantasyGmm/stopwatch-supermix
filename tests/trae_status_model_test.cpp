#include "apps/app_codex_status/trae_status_model.h"

#include <cassert>
#include <cstring>
#include <string>


static bool parse(
    const char* json,
    uint32_t received_at_ms,
    TraeStatusSnapshot& snapshot
)
{
    return parse_trae_status_message(
        json,
        std::strlen(json),
        received_at_ms,
        snapshot
    );
}


int main()
{
    TraeStatusSnapshot snapshot;
    assert(parse(
        R"({"type":"status","source":"trae","trae_online":true,"trae_model":"Model A","trae_effort":"high","trae_revision":12,"trae_tasks":[{"project":"StopWatch","title":"Pending task","detail":"Question","time":"10:00","state":"pending"},{"project":"Docs","title":"Running task","detail":"Writing","time":"09:59","state":"running"},{"project":"Web","title":"Completed task","detail":"Done","time":"09:58","state":"completed"}]})",
        4321,
        snapshot
    ));
    assert(snapshot.online);
    assert(std::string(snapshot.model.data()) == "Model A");
    assert(std::string(snapshot.effort.data()) == "high");
    assert(snapshot.revision == 12);
    assert(snapshot.received_at_ms == 4321);
    assert(snapshot.task_count == 3);
    assert(snapshot.tasks[0].state == TraeTaskState::Pending);
    assert(snapshot.tasks[1].state == TraeTaskState::Running);
    assert(snapshot.tasks[2].state == TraeTaskState::Completed);

    std::string mutable_json =
        R"({"type":"status","source":"trae","trae_online":true,"trae_model":"Owned","trae_effort":"medium","trae_revision":13,"trae_tasks":[{"project":"Project","title":"Title","detail":"Detail","time":"10:01","state":"running"}]})";
    assert(parse_trae_status_message(
        mutable_json.data(),
        mutable_json.size(),
        5000,
        snapshot
    ));
    mutable_json.assign(mutable_json.size(), 'x');
    assert(std::string(snapshot.model.data()) == "Owned");
    assert(std::string(snapshot.tasks[0].project.data()) == "Project");

    const TraeStatusSnapshot before_invalid = snapshot;
    assert(!parse(
        R"({"type":"status","source":"trae","trae_online":true,"trae_model":"Bad","trae_effort":"high","trae_revision":14,"trae_tasks":[{"project":"P","title":"T","detail":"D","time":"10:00","state":"unknown"}]})",
        6000,
        snapshot
    ));
    assert(snapshot.revision == before_invalid.revision);
    assert(std::string(snapshot.model.data()) ==
           std::string(before_invalid.model.data()));

    assert(!parse(
        R"({"type":"event","source":"trae","trae_online":true,"trae_model":"Bad","trae_effort":"high","trae_revision":15,"trae_tasks":[]})",
        6001,
        snapshot
    ));
    assert(!parse(
        R"({"type":"status","source":"trae","trae_online":true,"trae_model":"Bad","trae_effort":"high","trae_revision":15,"trae_tasks":"bad"})",
        6002,
        snapshot
    ));

    std::string five_tasks =
        R"({"type":"status","source":"trae","trae_online":true,"trae_model":"Model","trae_effort":"high","trae_revision":16,"trae_tasks":[)";
    for (int index = 0; index < 5; ++index) {
        if (index != 0) {
            five_tasks += ",";
        }
        five_tasks +=
            R"({"project":"P","title":"T","detail":"D","time":"10:00","state":"running"})";
    }
    five_tasks += "]}";
    assert(parse_trae_status_message(
        five_tasks.data(),
        five_tasks.size(),
        7000,
        snapshot
    ));
    assert(snapshot.task_count == 4);
}
