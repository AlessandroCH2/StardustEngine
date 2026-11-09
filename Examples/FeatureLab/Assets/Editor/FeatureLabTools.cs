using Stardust;
using Stardust.Editor;
[CustomEditor(typeof(FeatureLabController))]
public class FeatureLabInspector : Editor {
    public override void OnInspectorGUI(){var game=(FeatureLabController)target;EditorGUILayout.LabelField("Feature Lab Controls");game.SpinSpeed=EditorGUILayout.FloatField("Rotation speed",game.SpinSpeed);game.ShowWater=EditorGUILayout.Toggle("Show water",game.ShowWater);EditorGUILayout.HelpBox("Default fields below keep drag-and-drop entity references available.");DrawDefaultInspector();}
}
public class FeatureLabWindow : EditorWindow {
    Vector3 position=new Vector3(0,1,0);
    [MenuItem("Tools/Feature Lab")] public static void ShowTool()=>GetWindow<FeatureLabWindow>("Feature Lab Tools");
    public override void OnGUI(){EditorGUILayout.LabelField("Custom C# Editor Window");position=EditorGUILayout.Vector3Field("Spawn position",position);if(GUILayout.Button("Create cube")){var cube=Entity.CreatePrimitive(PrimitiveType.Cube,"Created by Editor script");cube.transform.position=position;}EditorGUILayout.HelpBox("This tool lives in an Editor folder and is excluded from players.");}
}
