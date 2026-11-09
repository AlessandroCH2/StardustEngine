using Stardust;
using Stardust.UI;
public class FeatureLabController : ScriptBehaviour {
    public Entity Camera,Actor,Water,Status,Video;
    public float SpinSpeed=30;
    public bool ShowWater=true;
    float clock;
    void OnCreate(){Camera.transform.LookAt(new Vector3(0,0,0));var pyramid=Entity.Create("Mesh built from C#");pyramid.transform.position=new Vector3(-3,0,0);var mesh=pyramid.AddComponent<MeshRenderer>();mesh.SetMesh(new[]{new Vector3(-1,0,-1),new Vector3(1,0,-1),new Vector3(1,0,1),new Vector3(-1,0,1),new Vector3(0,3,0)},new[]{0,4,1,1,4,2,2,4,3,3,4,0,0,1,2,2,3,0});mesh.color=new Color(.9f,.35f,.1f);mesh.roughness=.35f;}
    void OnUpdate(float dt){clock+=dt;Actor.transform.Rotate(new Vector3(0,SpinSpeed*dt,0));Water.SetActive(ShowWater);if(Input.GetButtonDown("Jump"))ToggleWater();if(Status!=null)Status.GetComponent<Text>().text=$"Render texture: 256 x 192 | Layer 3 excluded from minimap\nGamepad: {(Input.IsGamepadConnected()?"connected":"not connected")} | Space / South button toggles water";}
    public void ToggleWater()=>ShowWater=!ShowWater;
    public void ToggleVideo(){var player=Video.GetComponent<VideoPlayer>();if(player.isPlaying)player.Pause();else player.Play();}
}
