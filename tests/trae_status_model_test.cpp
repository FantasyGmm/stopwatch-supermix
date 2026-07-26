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
        R"({"type":"status","source":"trae","trae_online":true,"trae_process_state":"live","trae_model":"Fallback","trae_effort":"medium","trae_revision":20,"trae_pending_count":6,"trae_running_count":3,"trae_tasks_truncated":true,"trae_tasks":[{"id":"abc123","project":"StopWatch","title":"Confirm","detail":"Waiting","time":"10:00","state":"pending","model":"Doubao","effort":"High"},{"id":"","project":"StopWatch","title":"Blank id","detail":"Allowed","time":"10:01","state":"running"}]})",
        8000,
        snapshot
    ));
    assert(snapshot.process_state == TraeProcessState::Live);
    assert(snapshot.pending_count == 6);
    assert(snapshot.running_count == 3);
    assert(snapshot.tasks_truncated);
    assert(std::string(snapshot.tasks[0].id.data()) == "abc123");
    assert(std::string(snapshot.tasks[0].model.data()) == "Doubao");
    assert(std::string(snapshot.tasks[0].effort.data()) == "High");
    assert(std::string(snapshot.tasks[1].id.data()).empty());

    assert(parse(
        R"({"type":"status","source":"trae","trae_online":true,"trae_model":"Model A","trae_effort":"high","trae_revision":-12,"trae_tasks":[{"project":"StopWatch","title":"Pending task","detail":"Question","time":"10:00","state":"pending"},{"project":"Docs","title":"Running task","detail":"Writing","time":"09:59","state":"running"},{"project":"Web","title":"Completed task","detail":"Done","time":"09:58","state":"completed"}]})",
        4321,
        snapshot
    ));
    assert(snapshot.online);
    assert(std::string(snapshot.model.data()) == "Model A");
    assert(std::string(snapshot.effort.data()) == "high");
    assert(snapshot.revision == -12);
    assert(snapshot.received_at_ms == 4321);
    assert(snapshot.process_state == TraeProcessState::Live);
    assert(snapshot.pending_count == 1);
    assert(snapshot.running_count == 1);
    assert(!snapshot.tasks_truncated);
    assert(snapshot.task_count == 3);
    assert(std::string(snapshot.tasks[0].id.data()) == "legacy-0");
    assert(std::string(snapshot.tasks[1].id.data()) == "legacy-1");
    assert(std::string(snapshot.tasks[2].id.data()) == "legacy-2");
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

    const TraeStatusSnapshot before_malformed_new_fields = snapshot;
    assert(!parse(
        R"({"type":"status","source":"trae","trae_online":false,"trae_process_state":"busy","trae_model":"Bad","trae_effort":"low","trae_revision":17,"trae_pending_count":0,"trae_running_count":0,"trae_tasks_truncated":false,"trae_tasks":[]})",
        8001,
        snapshot
    ));
    assert(!parse(
        R"({"type":"status","source":"trae","trae_online":false,"trae_model":"Bad","trae_effort":"low","trae_revision":17,"trae_pending_count":-1,"trae_tasks":[]})",
        8002,
        snapshot
    ));
    assert(!parse(
        R"({"type":"status","source":"trae","trae_online":false,"trae_model":"Bad","trae_effort":"low","trae_revision":17,"trae_tasks_truncated":"false","trae_tasks":[]})",
        8003,
        snapshot
    ));
    assert(snapshot.revision == before_malformed_new_fields.revision);
    assert(snapshot.task_count == before_malformed_new_fields.task_count);
}
