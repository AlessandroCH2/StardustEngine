using Stardust;
namespace Stardust.Engine {
// Optional motion for water volumes (material waves are handled by Stardust/Water).
public class WaterSurface : ScriptBehaviour {
    public float Level;
    public float TideAmplitude;
    public float TideSpeed=.1f;
    void OnUpdate(float dt){var p=transform.position;p.y=Level+Mathf.Sin(Time.time*TideSpeed)*TideAmplitude;transform.position=p;}
    public bool IsUnderwater(Entity body)=>body!=null&&body.transform.position.y<transform.position.y;
}
}
